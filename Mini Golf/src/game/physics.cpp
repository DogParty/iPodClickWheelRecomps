// The ball: how it moves on the course, what the course does to it, and what it hits.
//
// Positions are 16.16 in tile units (the hole is a 256×256 tile map, TILE_MAP, one byte per
// tile); velocities are 16.16 per step. A frame's step (0x18009f28) splits into sub-steps by
// speed so a fast ball cannot tunnel through a wall; each sub-step (0x1800d1c4) reads the
// tile under the ball — out of bounds, water, a cup, a bumper, a slope, a special surface —
// moves it, applies friction and drag, and bounces it off the nearest wall, mesh or peg.
//
// Long because a step of the ball is one calculation: the surface under it, the slope, the
// walls, the meshes and the pegs all act on the same position and velocity within one frame.
#include "physics.h"

#include "calling.h"
#include "fixed.h"
#include "game_state.h"
#include "hole_tick.h"
#include "libc.h"
#include "records.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "screens.h"
#include "state.h"

namespace minigolf::game {

namespace {

// Still recompiled, named by their use here (inferred).

enum Tile : uint32_t { OUT_OF_BOUNDS = 0, OUT_OF_BOUNDS_ALT = 1, GREEN = 2, WATER = 6 };
constexpr uint32_t SURFACE_FIRST = 0x30, SURFACE_COUNT = 0x10;    // tiles 0x30..0x3f: surfaces
constexpr uint32_t OBSTACLE_FIRST = 0x40, OBSTACLE_COUNT = 0x40;  // tiles 0x40..0x7f: obstacles
constexpr uint32_t SLOPE_FIRST = 0x80;     // tiles 0x80..0xff: slopes, direction in the high nibble
constexpr uint32_t SLOPE_DIAGONAL = 0xb5;  // cos 45° in 8.8
constexpr uint32_t SOUND_BOUNCE = 1, SOUND_WATER = 3, SOUND_CUP = 4, SOUND_OUT = 5;
constexpr uint32_t CUP_SINK_FRAMES = 0x14, SURFACE_MESH = 4, SURFACE_SINK = 6, SURFACE_WARP = 7;
constexpr uint32_t SIN_TABLE = 0x1801a5b4;  // 257 words: sin of 0..90° in 16.16, 1024 per circle
constexpr uint32_t TURNS_PER_DEGREE = 0x2d82d;  // 1024/360 in 16.16
constexpr uint32_t WALL_REACH = 0x220, PEG_REACH = 0x484;

enum ObstacleKind : uint32_t {
    CUP = 1,
    BUMPER_SMALL = 2,
    BUMPER_LARGE = 3,
    RIPPLE = 4,
    BUMPER_SQUARE = 5,
    WIDE_CUP_FIRST = 6,   // 6..9: cups that behave as a surface once entered
    FALSE_CUP_FIRST = 10  // 10..13: the same, smaller
};

}  // namespace

uint32_t integer_sqrt(uint32_t n);
int32_t sine_degrees(int32_t degrees);
int32_t power_meter_value();
void hint_sequence_start(uint32_t frames_after, uint32_t state_after);
void ball_to_tee();
void surface_apply(uint32_t surface);
uint32_t mesh_collide(int32_t x, int32_t y, uint32_t visit);
void trail_reset();

namespace {

int32_t vx() {
    return static_cast<int32_t>(play_state().velocity_x);
}
int32_t vy() {
    return static_cast<int32_t>(play_state().velocity_y);
}
void set_vx(int32_t v) {
    play_state().velocity_x = static_cast<uint32_t>(v);
}
void set_vy(int32_t v) {
    play_state().velocity_y = static_cast<uint32_t>(v);
}
int32_t ball_x() {
    return static_cast<int32_t>(play_state().ball_x);
}
int32_t ball_y() {
    return static_cast<int32_t>(play_state().ball_y);
}
int32_t speed_squared() {
    return (vx() >> 8) * (vx() >> 8) + (vy() >> 8) * (vy() >> 8);
}
int32_t sine(uint32_t angle) {
    return static_cast<int32_t>(guest_array<int16_t>(SINE_TABLE)[angle & 0xff]);
}

int32_t isqrt(int32_t n) {
    return static_cast<int32_t>(integer_sqrt(static_cast<uint32_t>(n)));
}
int32_t divide(int32_t a, int32_t b) {
    return static_cast<int32_t>(
        libc::signed_divide(static_cast<uint32_t>(a), static_cast<uint32_t>(b)).quotient);
}
const Obstacle& obstacle(uint32_t tile) {
    return obstacle_at(tile - OBSTACLE_FIRST);
}
int32_t obstacle_x(uint32_t tile) {
    return obstacle(tile).x;
}
int32_t obstacle_y(uint32_t tile) {
    return obstacle(tile).y;
}
uint32_t obstacle_kind(uint32_t tile) {
    return obstacle(tile).kind;
}
int32_t wall_count() {
    return static_cast<int16_t>(players_state().wall_count);
}
int32_t peg_count() {
    return static_cast<int16_t>(players_state().peg_count);
}

// A cup draws the ball in: the pull, and whether it has fallen.
enum class CupResult { None, Fell, Pulled };

// The cup's capture: `radius` is the reach in 8.8 below which the ball drops; the pull acts
// out to `pull_reach`. `centre` is the cup's centre offset in tiles, `pull_shift` the pull's
// strength.
CupResult cup_capture(uint32_t tile, int32_t x, int32_t y, int32_t centre, int32_t capture_base,
                      int32_t speed_shift, int32_t min_capture, int32_t pull_reach,
                      int32_t pull_shift) {
    const int32_t speed = isqrt(speed_squared());
    const int32_t dx = x - obstacle_x(tile) - centre, dy = y - obstacle_y(tile) - centre;
    const int32_t distance = isqrt((dx * dx + dy * dy) << 16);
    if (min_capture >= 0 && speed >= 0x700) {
        return CupResult::None;
    }
    if (capture_base - (speed >> speed_shift) > distance ||
        (min_capture >= 0 && distance < min_capture)) {
        return CupResult::Fell;
    }
    if (distance >= pull_reach) {
        return CupResult::None;
    }
    const int32_t strength = speed >> pull_shift;
    const int32_t offset = centre == 2 ? 4 : 8;
    set_vx(vx() - strength * divide((x - obstacle_x(tile) - offset) << 16, distance));
    set_vy(vy() - strength * divide((y - obstacle_y(tile) - offset) << 16, distance));
    return CupResult::Pulled;
}

// A round bumper: inside `radius_squared` of its centre the ball is thrown outward.
bool bumper(uint32_t tile, int32_t x, int32_t y, int32_t centre, int32_t radius_squared,
            int32_t shift, int32_t kick) {
    const int32_t dx = x - obstacle_x(tile), dy = y - obstacle_y(tile);
    if ((dx - centre) * (dx - centre) + (dy - centre) * (dy - centre) >= radius_squared) {
        return false;
    }
    set_vx(vx() + (dx << shift) - kick);
    set_vy(vy() + (dy << shift) - kick);
    return true;
}

// The tile under the ball, and what that means: returns the tile to treat the ball as being
// on (a cup or bumper may redirect to a surface tile), or 0x100 + 1 when the sub-step is over.
constexpr uint32_t STEP_DONE = 0x100;

uint32_t obstacle_apply(uint32_t tile, int32_t x, int32_t y) {
    const uint32_t kind = obstacle_kind(tile);
    switch (kind) {
    case CUP:
    case FALSE_CUP_FIRST:
    case FALSE_CUP_FIRST + 1:
    case FALSE_CUP_FIRST + 2:
    case FALSE_CUP_FIRST + 3: {
        switch (cup_capture(tile, x, y, 2, 0x280, 3, 0x60, 0x400, 4)) {
        case CupResult::Fell:
            if (kind != CUP) {
                return ((0xb - kind) + 0x3a) & 0xff;  // a false cup is a surface
            }
            hint_sequence_start(CUP_SINK_FRAMES, IDLE);
            menu_sound_play(SOUND_CUP);
            return STEP_DONE;
        default:
            return tile;
        }
    }
    case BUMPER_SMALL:
        bumper(tile, x, y, 5, 0x19, 13, 0xa000);
        return tile;
    case BUMPER_LARGE:
        bumper(tile, x, y, 7, 0x31, 12, 0x7000);
        return tile;
    case RIPPLE: {  // a wave across the tile, by how far down it the ball is
        const int32_t phase = ((y - obstacle_y(tile)) * 0x51e00) >> 16;
        set_vy(vy() - (sine(static_cast<uint32_t>(phase)) >> 1));
        return tile;
    }
    case BUMPER_SQUARE: {
        const int32_t speed = isqrt(speed_squared());
        const int32_t dx = x - obstacle_x(tile) - 8, dy = y - obstacle_y(tile) - 8;
        const int32_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
        if (ax < ay) {  // above or below
            if (static_cast<uint32_t>(dy + 1) <= 2 && speed < 0x4b0) {
                return 0x3f;  // too slow to bounce: the surface stops it
            }
            set_vy(vy() - (dy << 13));
        } else {
            if (static_cast<uint32_t>(dx + 1) <= 2 && speed < 0x4b0) {
                return 0x3f;
            }
            set_vx(vx() - (dx << 13));
        }
        return tile;
    }
    case WIDE_CUP_FIRST:
    case WIDE_CUP_FIRST + 1:
    case WIDE_CUP_FIRST + 2:
    case WIDE_CUP_FIRST + 3: {
        switch (cup_capture(tile, x, y, 8, 0x300, 1, -1, 0x800, 3)) {
        case CupResult::Fell:
            return ((0xf - kind) + 0x36) & 0xff;
        default:
            return tile;
        }
    }
    default:
        return tile;
    }
}

// A slope: acceleration down it, in one of eight directions, steeper the higher its index.
void slope_apply(uint32_t tile, uint32_t shift) {
    const int32_t steepness = static_cast<int32_t>(tile & 0xf);
    int32_t pull = (0x10 - steepness) * static_cast<int32_t>(play_state().slope_gentle) +
                   steepness * static_cast<int32_t>(play_state().slope_steep);
    pull = (pull >> shift) >> 4;
    const int32_t diagonal = (pull * static_cast<int32_t>(SLOPE_DIAGONAL)) >> 8;
    switch ((tile >> 4) - 8) {
    case 0:
        set_vx(vx() + pull);
        break;
    case 1:
        set_vx(vx() + diagonal);
        set_vy(vy() - diagonal);
        break;
    case 2:
        set_vy(vy() - pull);
        break;
    case 3:
        set_vx(vx() - diagonal);
        set_vy(vy() - diagonal);
        break;
    case 4:
        set_vx(vx() - pull);
        break;
    case 5:
        set_vx(vx() - diagonal);
        set_vy(vy() + diagonal);
        break;
    case 6:
        set_vy(vy() + pull);
        break;
    case 7:
        set_vx(vx() + diagonal);
        set_vy(vy() + diagonal);
        break;
    default:
        break;
    }
}

// Reflect the velocity about a normal (8.8 components) and damp it by the restitution.
void bounce(int32_t nx, int32_t ny) {
    const int32_t along = (-(nx * vx() + ny * vy())) >> 8;  // negated before the shift
    const int32_t kick_y = (along * ny) >> 8, kick_x = (along * nx) >> 8;
    set_vy(vy() + kick_y + kick_y);
    set_vx(vx() + kick_x + kick_x);
}
int32_t damped(int32_t v) {
    return ((static_cast<int32_t>(play_state().restitution) >> 4) * (v >> 4)) >> 8;
}

// The nearest wall in front of the ball, within WALL_REACH along its normal; -1 if none.
int32_t nearest_wall(int32_t x, int32_t y, int32_t& distance) {
    int32_t best = -1;
    distance = static_cast<int32_t>(WALL_REACH);
    const int32_t count = wall_count();
    for (int32_t i = 0; i < count; ++i) {
        const Wall& wall = wall_at(static_cast<uint32_t>(i));
        const int32_t x0 = wall.x0, y0 = wall.y0, x1 = wall.x1, y1 = wall.y1;
        const int32_t nx = wall.nx, ny = wall.ny;
        const int32_t d = (x - x0) * nx + (y - y0) * ny;
        if (d <= -0x200 || d >= distance) {
            continue;
        }
        if ((x1 - x0) * (x - x0) + (y1 - y0) * (y - y0) <= 0) {
            continue;  // beyond its start
        }
        if ((x - x1) * (x0 - x1) + (y - y1) * (y0 - y1) <= 0) {
            continue;  // beyond its end
        }
        best = i;
        distance = d;
    }
    return best;
}

}  // namespace

// 0x1800d1c4 — one sub-step of the ball, moving it by its velocity >> `shift` (0xff: no
// movement, only the tile's effect). Returns 1 when it bounced off something.
uint32_t ball_move(uint32_t shift) {
    const uint32_t state = play_state().state;
    if (state != FAST_FORWARD && state != ROLLING) {
        return 0;
    }
    const auto clamp = [](int32_t v) { return v < 0 ? 0 : v > 0xff ? 0xff : v; };
    int32_t x = clamp(ball_x() >> 16), y = clamp(ball_y() >> 16);
    uint32_t tile = tile_at(static_cast<uint32_t>(x), static_cast<uint32_t>(y));

    switch (tile) {
    case WATER:
        menu_sound_play(SOUND_WATER);
        [[fallthrough]];
    case OUT_OF_BOUNDS:
    case OUT_OF_BOUNDS_ALT: {  // back to the tee, with the out-of-bounds message
        ball_to_tee();
        if (tile != WATER) {
            menu_sound_play(SOUND_OUT);
        }
        const uint32_t player = static_cast<uint32_t>(players_state().current);
        player_record(player).placed = 1;
        play_state().state = OUT_MESSAGE;
        break;
    }
    case 2:
    case 3:
    case 4:
    case 5:
        break;
    default:
        if (tile >= OBSTACLE_FIRST && tile < OBSTACLE_FIRST + OBSTACLE_COUNT) {
            tile = obstacle_apply(tile, x, y);
            if (tile == STEP_DONE) {
                return 0;
            }
        }
        if (tile >= SURFACE_FIRST && tile < SURFACE_FIRST + SURFACE_COUNT) {
            surface_apply(tile - SURFACE_FIRST);
            return 0;
        }
        if (tile >= SLOPE_FIRST) {
            slope_apply(tile, shift);
        }
        break;
    }
    if (shift == 0xff) {
        return 0;
    }

    // Move, then friction and drag.
    play_state().ball_x = static_cast<uint32_t>(ball_x() + (vx() >> shift));
    play_state().ball_y = static_cast<uint32_t>(ball_y() + (vy() >> shift));
    x = ball_x() >> 16;
    y = ball_y() >> 16;
    const int32_t friction = static_cast<int32_t>(play_state().friction);
    const auto rubbed = [&](int32_t v) {
        return ((v << shift) - v + (((friction >> 8) * v) >> 8)) >> shift;
    };
    set_vx(rubbed(vx()));
    set_vy(rubbed(vy()));
    const int32_t drag = (0x190 * ((0x10000 - friction) >> 2)) >> 8;
    int32_t speed = isqrt(speed_squared());
    if (speed == 0) {
        speed = 1;
    }
    set_vx(vx() - ((drag * divide(vx(), speed)) >> 8));
    set_vy(vy() - ((drag * divide(vy(), speed)) >> 8));

    // Walls first.
    int32_t distance;
    const int32_t wall_index = nearest_wall(x, y, distance);
    if (wall_index >= 0) {
        const Wall& wall = wall_at(static_cast<uint32_t>(wall_index));
        const int32_t nx = wall.nx, ny = wall.ny;
        bounce(nx, ny);
        const int32_t push = static_cast<int32_t>(WALL_REACH) - distance;
        play_state().ball_x = static_cast<uint32_t>(ball_x() + push * nx);
        play_state().ball_y = static_cast<uint32_t>(ball_y() + push * ny);
        set_vx(damped(vx()));
        set_vy(damped(vy()));
        return 1;
    }

    // Then the meshes the player has passed, and the course's own shapes behind them.
    for (uint32_t visit = 0; visit < MESH_VISIT_COUNT; ++visit) {
        if (mesh_seen(visit) == 0) {
            continue;
        }
        if (mesh_collide(x, y, visit) != 0) {
            return 1;
        }
    }
    for (uint32_t visit = 0; visit < MESH_VISIT_COUNT; ++visit) {
        if (mesh_seen(visit) == 0) {
            continue;
        }
        const MeshVisit& entry = mesh_visit(visit);
        mesh_seen(visit) = 0;
        if (entry.solid == 0) {
            continue;
        }
        const int32_t x0 = entry.x, y0 = entry.y;
        const int32_t w = entry.width, h = entry.height;
        if (x >= x0 && y >= y0 && x0 + w > x && y0 + h > y) {
            surface_apply(SURFACE_MESH);
        }
    }

    // Then the pegs.
    int32_t best = -1, best_distance = static_cast<int32_t>(PEG_REACH);
    const int32_t pegs = peg_count();
    for (int32_t i = 0; i < pegs; ++i) {
        const Peg& peg = peg_at(static_cast<uint32_t>(i));
        const int32_t dx = peg.x - x, dy = peg.y - y;
        const int32_t d = (dx * dx + dy * dy) << 8;
        if (d < best_distance) {
            best = i;
            best_distance = d;
        }
    }
    if (best < 0) {
        return 0;
    }
    const Peg& peg = peg_at(static_cast<uint32_t>(best));
    bounce(peg.nx, peg.ny);
    // The ball bounced off the peg, but the push-out that separates them uses the normal of the
    // *last wall in the table* — the register the original still had the wall loop's last read
    // in. It is not a mistake worth correcting: the holes are built around the behaviour it
    // produces. A hole with no walls has no such normal; none ship that way, and the original
    // would have read off the front of the table looking for one.
    const int32_t push = static_cast<int32_t>(WALL_REACH) - isqrt(best_distance << 8);
    if (wall_count() > 0) {
        const Wall& last_wall = wall_at(static_cast<uint32_t>(wall_count() - 1));
        play_state().ball_x = static_cast<uint32_t>(ball_x() + push * last_wall.nx);
        play_state().ball_y = static_cast<uint32_t>(ball_y() + push * last_wall.ny);
    }
    set_vx(damped(vx()));
    set_vy(damped(vy()));
    return 1;
}

// 0x18009f28 — a frame's worth of movement: 1, 2, 4, 8 or 16 sub-steps by speed. Returns
// whether anything was hit (and plays the bounce if so).
uint32_t ball_step() {
    const int32_t energy = speed_squared();
    uint32_t shift = 0;
    for (const int32_t threshold : {0x40000, 0x100000, 0x400000, 0x1000000}) {
        if (energy >= threshold) {
            ++shift;
        }
    }
    uint32_t hit = 0;
    for (uint32_t i = 0; i < (1u << shift); ++i) {
        hit |= ball_move(shift);
    }
    if (hit != 0) {
        menu_sound_play(SOUND_BOUNCE);
    }
    return hit;
}

// 0x1800ce04 — is a point on the course somewhere the ball cannot be: off the green, on a
// surface or obstacle tile (except inside a cup's rim), or behind a wall? Used to stop the aim
// line where it would hit something.
uint32_t point_blocked(int32_t x, int32_t y) {
    if (x < 0 || y < 0 || x > 0xff || y > 0xff) {
        return 1;
    }
    const uint32_t tile = tile_at(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
    if (tile == GREEN) {
        return 1;
    }
    if (tile >= SURFACE_FIRST && tile < SURFACE_FIRST + SURFACE_COUNT) {
        return 1;
    }
    if (tile >= OBSTACLE_FIRST && tile < OBSTACLE_FIRST + OBSTACLE_COUNT &&
        obstacle_kind(tile) == CUP) {
        const int32_t dx = x - obstacle_x(tile) - 4, dy = y - obstacle_y(tile) - 4;
        if (dx * dx + dy * dy <= 0x14) {
            return 1;
        }
    }
    int32_t distance;
    return nearest_wall(x, y, distance) < 0 ? 0 : 1;
}

// 0x18010588 — fill the trail with the ball's position: a fresh stroke leaves no tail.
void trail_reset() {
    play_state().trail_length = 0;
    play_state().trail_index = static_cast<uint8_t>(0);
    for (uint32_t i = 0; i < 64; ++i) {
        play_state().trail_x[i] = static_cast<uint16_t>(play_state().ball_x >> 16);
        play_state().trail_y[i] = static_cast<uint16_t>(play_state().ball_y >> 16);
    }
}

// 0x18014e44 — the integer square root, bit by bit from the top.
uint32_t integer_sqrt(uint32_t n) {
    uint32_t root = 0;
    if (n >= 0x4000'0000u) {
        root = 0x8000;
        n -= 0x4000'0000u;
    }
    for (int32_t bit = 14; bit > 0; --bit) {
        const uint32_t trial = (root + (1u << (bit - 1))) << (bit + 1);
        if (n >= trial) {
            root |= 1u << bit;
            n -= trial;
        }
    }
    if (n > (root << 1)) {
        root |= 1;
    }
    return root;
}

// 0x18007678 — sin of an angle in 16.16 degrees, 16.16 result, from a quarter-wave table with
// linear interpolation. Exact at 0°, 90°, 180° and 270°.
int32_t sine_degrees(int32_t degrees) {
    while (degrees < 0) {
        degrees += to_fixed_signed(360);
    }
    while (degrees > to_fixed_signed(360)) {
        degrees -= to_fixed_signed(360);
    }
    if (degrees == 0 || degrees == to_fixed_signed(180)) {
        return 0;
    }
    if (degrees == to_fixed_signed(90)) {
        return 0x10000;
    }
    if (degrees == to_fixed_signed(270)) {
        return -0x10000;
    }
    const int64_t turns =
        (static_cast<int64_t>(degrees) * TURNS_PER_DEGREE) >> 16;  // 1024 per circle
    uint32_t t = static_cast<uint32_t>(turns);
    uint32_t index = (t >> 16) & 0xff;
    if ((t & 0x100'0000u) != 0) {  // second and fourth quarters run backwards
        index ^= 0xff;
        t = ~t;
    }
    const int32_t low = static_cast<int32_t>(guest_array<uint32_t>(SIN_TABLE)[index]);
    const int32_t high = static_cast<int32_t>(guest_array<uint32_t>(SIN_TABLE)[index + 1]);
    const int64_t fraction = t & 0xffff;
    int32_t value = static_cast<int32_t>((fraction * (high - low)) >> 16) + low;
    if ((static_cast<uint32_t>(turns) & 0x200'0000u) != 0) {  // second half: negative
        value = -value;
    }
    return value;
}

// 0x18015440 — the power meter's reading: a bounce between 0x200 and 0x7fff that follows the
// sine of the frames spent charging.
int32_t power_meter_value() {
    const uint32_t frames = play_state().state_frames;
    const uint32_t angle = 0x40 + ((frames << 1) & 0x7e);
    int32_t swing = sine(angle) << 1;
    if (swing < 0) {
        swing = -swing;
    }
    int32_t value = (0x10e * ((0x10000 - swing) >> 1)) >> 8;
    if (value < 0x200) {
        value = 0x200;
    } else if (value >= 0x8000) {
        value = 0x7fff;
    }
    return static_cast<int16_t>(value);
}

// 0x18015cec — the ball has arrived somewhere: run the hint sequence, then take up
// `state_after` with `frames_after` on its clock.
void hint_sequence_start(uint32_t frames_after, uint32_t state_after) {
    play_state().ball_arrived = static_cast<uint8_t>(1);
    play_state().state = HINT_START;
    play_state().saved_frames = state_after;
    play_state().saved_state = frames_after;
}

// 0x180112c0 — put the ball at the centre of the tee: the average of the tee edges' ends.
void ball_to_tee() {
    play_state().ball_x = 0;
    play_state().ball_y = 0;
    play_state().aim_angle = 0x40;
    const int32_t edges = static_cast<int16_t>(players_state().tee_edge_count);
    int32_t ends = 0;
    for (int32_t i = 0; i < edges; ++i) {
        const TeeEdge& edge = tee_edge_at(static_cast<uint32_t>(i));
        if (edge.kind_up != 4 && edge.kind_down != 4) {
            continue;
        }
        for (const int16_t end : {edge.a, edge.b}) {
            const TeePoint& point = tee_point_at(static_cast<uint32_t>(end));
            play_state().ball_x = static_cast<uint32_t>(ball_x() + point.x);
            play_state().ball_y = static_cast<uint32_t>(ball_y() + point.y);
        }
        ends = static_cast<int16_t>(ends + 2);
    }
    if (ends == 0) {
        return;
    }
    play_state().ball_x = static_cast<uint32_t>(divide(ball_x() << 16, ends));
    play_state().ball_y = static_cast<uint32_t>(divide(ball_y() << 16, ends));
}

// 0x18015050 — a special surface under the ball (tiles 0x30..0x3f, and the surface a cup or
// mesh turns into): its entry says how the velocity changes and where the ball goes next. The
// ball then sinks from where it was to that target (SINK_FROM/TO) over the hint sequence.
void surface_apply(uint32_t surface) {
    const uint32_t from_x = play_state().ball_x, from_y = play_state().ball_y;
    play_state().sink_from_x = from_x;
    play_state().sink_from_y = from_y;
    const Surface& entry = surface_at(surface);
    const uint32_t flags = entry.flags;
    enum : uint32_t {
        KEEP_X = 1,
        KEEP_Y = 2,
        KEEP_VX = 0x4,
        KEEP_VY = 0x8,
        FLIP_VX = 0x10,
        FLIP_VY = 0x20,
        WARP = 0x40,
        STAY = 0x80,
        SCALE_VX = 0x100,
        SCALE_VY = 0x200
    };
    if ((flags & KEEP_X) == 0) {
        play_state().ball_x = 0;
    }
    if ((flags & KEEP_Y) == 0) {
        play_state().ball_y = 0;
    }
    if ((flags & (KEEP_VX | SCALE_VX)) == 0) {
        set_vx(0);
    }
    if ((flags & (KEEP_VY | SCALE_VY)) == 0) {
        set_vy(0);
    }
    if ((flags & FLIP_VX) != 0) {
        set_vx(-vx());
    }
    if ((flags & FLIP_VY) != 0) {
        set_vy(-vy());
    }
    const int32_t ax = entry.ax, ay = entry.ay;
    set_vx((flags & SCALE_VX) != 0 ? (ax * (vx() >> 8)) >> 8 : vx() + ax);
    set_vy((flags & SCALE_VY) != 0 ? (ay * (vy() >> 8)) >> 8 : vy() + ay);
    play_state().ball_x = play_state().ball_x + (to_fixed(entry.dx));
    play_state().ball_y = play_state().ball_y + (to_fixed(entry.dy));
    if (play_state().state == FAST_FORWARD) {
        trail_reset();
        return;
    }
    if ((flags & STAY) == 0) {  // the ball sinks to the target rather than jumping there
        play_state().sink_to_x = play_state().ball_x;
        play_state().sink_to_y = play_state().ball_y;
        play_state().ball_x = from_x;
        play_state().ball_y = from_y;
        hint_sequence_start((flags & WARP) != 0 ? SURFACE_SINK : SURFACE_WARP, IDLE);
    }
    menu_sound_play(2);
}

// 0x18012c0c — bounce the ball off a mesh's rectangle if it is just outside one of its sides
// (within two tiles): the ball is moved clear and the velocity reflected and damped.
uint32_t mesh_collide(int32_t x, int32_t y, uint32_t visit) {
    const MeshVisit& entry = mesh_visit(visit);
    const int32_t x0 = entry.x, y0 = entry.y;
    const int32_t w = entry.width, h = entry.height;
    if (x >= x0 && x0 + w > x) {  // above or below it
        const int32_t new_vx = damped(vx()), new_vy = -damped(vy());
        int32_t clear_y;
        if (y <= y0) {
            if (y < y0 - 2) {
                return 0;
            }
            clear_y = y0 - 3;
        } else {
            if (y0 + h > y || y0 + h + 2 < y) {
                return 0;
            }
            clear_y = y0 + h + 3;
        }
        set_vx(new_vx);
        set_vy(new_vy);
        play_state().ball_y = to_fixed(clear_y);
        return 1;
    }
    if (y >= y0 && y0 + h > y) {  // beside it
        const int32_t new_vx = -damped(vx()), new_vy = damped(vy());
        int32_t clear_x;
        if (x <= x0) {
            if (x < x0 - 2) {
                return 0;
            }
            clear_x = x0 - 3;
        } else {
            if (x0 + w > x || x0 + w + 2 < x) {
                return 0;
            }
            clear_x = x0 + w + 3;
        }
        set_vx(new_vx);
        set_vy(new_vy);
        play_state().ball_x = to_fixed(clear_x);
        return 1;
    }
    return 0;
}

}  // namespace minigolf::game
