// Drawing a hole (0x1800a080): the course ground, its objects, the aim line, tee arrows, ball
// and arrow, the heads-up display, the score card, the unlocked-course picture, the power
// meter and the message panel — whichever of them the hole's state calls for. This port's
// ghost trail is drawn here too, for the reason the last paragraph gives.
//
// The ground is special: on the first frame of a hole the course texture is drawn with the
// camera's perspective through OpenGL ES and copied back into a screen-sized texture, and
// every later frame draws that copy as one quad. During the wipe to the next hole (state 30)
// the perspective quad is drawn directly, moving along a per-hole table.
//
// Long by design: everything a hole draws is here, and every part of it reads the same hole
// state. Splitting by drawn thing would mean threading that state across files for no gain.
#include "hole_render.h"

#include "calling.h"
#include "cheats.h"
#include "draw.h"
#include "fixed.h"
#include "framework/graphics.h"
#include "game_state.h"
#include "gl.h"
#include "gl_state.h"
#include "hole_load.h"
#include "hole_tick.h"
#include "libc.h"
#include "physics.h"
#include "random.h"
#include "records.h"
#include "resources.h"
#include "round_history.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "state.h"
#include "strings.h"
#include "text.h"
#include "ui.h"

namespace minigolf::game {

namespace {

void object_draw(uint32_t handle, HoleObject& object, ImageRecord& mesh, Blend blend);

// Still recompiled, named by their use here (inferred).
constexpr uint32_t TEXT_PANEL_TITLE = 0x64;

constexpr uint32_t TEXTURE_CAPTURE = 9, TEXTURE_GROUND = 10;  // texture names the ground uses
constexpr uint32_t GROUND_PIXELS = 0x1804'15e4;               // the screen-sized texture's store
constexpr uint32_t QUAD_COLORS = 0x1801'9af4, GROUND_COLORS = 0x1801'9b34,
                   GROUND_CORNERS = 0x1801'9b74;

constexpr int32_t CAMERA_X = 0x20, CAMERA_Y = -8;  // world to screen
constexpr uint32_t WIPE_TABLE =
    0x1803'3f90;  // 64 words per hole: the ground's rise during the wipe
constexpr uint32_t WIPE_WORDS_PER_HOLE = 64, WIPE_LAST = 0x3c;
constexpr uint32_t GROUND_RISE = 0x258;
constexpr uint32_t OBJECT_KINDS = GAME_STATE + 0x857c0, OBJECT_KIND_SIZE = 0x94;  // per kind
namespace kind {}                                                                 // namespace kind
namespace object {  // fields of a hole object (HOLE_OBJECTS table, 0x864 bytes each)

}  // namespace object
constexpr uint32_t MESHES = GAME_STATE + 0x8b944, MESH_SIZE = 60;
constexpr uint32_t KIND_LIMIT = 0x46, MESH_LIMIT = 0x100;
// Images (draw.h layout) the hole draws with.
constexpr uint32_t SPRITES = GAME_STATE + 0x84f74;  // ball frames and tee arrows
constexpr uint32_t TEE_MARKER = GAME_STATE + 0x85118;
constexpr uint32_t HOLE_BADGE = GAME_STATE + 0x84f38;  // also the meter's marker
constexpr uint32_t RESULT_BADGE = GAME_STATE + 0x850dc;
constexpr uint32_t METER_IMAGE = COURSE_PICTURES + 4 * course_picture::SIZE;
constexpr uint32_t ARROW_UP_IMAGE = GAME_STATE + 0x84ac4, ARROW_DOWN_IMAGE = GAME_STATE + 0x84b00;
// The aim arrow's sprites (a row of eight per colour, 25 texels each) and where each octant
// hangs from.
constexpr uint32_t ARROW_SPRITES = GAME_STATE + game_state::COURSE_TABLE + 0xa10;
constexpr uint32_t ARROW_SPRITE_WIDTH = 0x19;
// The golfer sprites keep their cell's texel column and height where a mesh keeps its U and V.
constexpr uint32_t ARROW_STEP_X = 0x1801'9a94,
                   ARROW_STEP_Y = 0x1801'9ab4;  // 16.16 per step inside an octant
constexpr uint32_t ARROW_OFFSET_X = 0x1801'9a84, ARROW_OFFSET_Y = 0x1801'9a8c;  // signed bytes
// Object records: the screen-space corners (+0 x, +0x200 y, 32 frames of four), the source
// quads, the rotation matrix and position.
constexpr int32_t SCREEN_CENTRE_Y = 0x78;
constexpr uint32_t BALL_WIDTH = 4, BALL_HEIGHT = 5, BALL_FRAME_MASK = 0xc;
constexpr uint32_t ARROW_WIDTH = 4, ARROW_HEIGHT = 5, MARKER_SIZE = 8;
constexpr uint32_t CARD_COLUMNS = 0x1801'9ad4;  // x of the four columns, single then two-player
constexpr int32_t CARD_SCROLL_X = 0x10e, CARD_ARROW_UP_Y = 0x47, CARD_ARROW_DOWN_Y = 0xb1;
constexpr uint32_t CARD_ROW_PITCH = 0xe;
constexpr uint32_t TEXT_CARD_TITLE = 0x3d, TEXT_CARD_HOLE = 0x3e, TEXT_CARD_PAR = 0x3f,
                   TEXT_CARD_STROKES = 0x40, TEXT_CARD_P1 = 0x41, TEXT_CARD_P2 = 0x42,
                   TEXT_CARD_TOTAL = 0x43, TEXT_HOLE_LABEL = 0x3f;
constexpr uint32_t TEXT_UNLOCK_1 = 0x49, TEXT_UNLOCK_2 = 0x61, TEXT_UNLOCK_3 = 0x62,
                   TEXT_UNLOCK_4 = 0x63;
constexpr uint32_t LITERAL_NEWLINE = 0x1800'b394, LITERAL_DASH = 0x1800'bce0,
                   LITERAL_SPACE = 0x1800'bce8, LITERAL_OPEN = 0x1800'bcec,
                   LITERAL_PLUS = 0x1800'bcf0, LITERAL_CLOSE = 0x1800'bcf4;
constexpr uint32_t WHITE = 0x10000, FULL = 0x10000;
// How strongly the ghost's dots show, and how big they are. The course art is loud — the greens
// are ringed with sand, stones and painted boards — so a single dim pixel a point disappears
// into it. These are the aim line's own two-by-two dots at a little over half its brightness:
// legible against the ground, and still clearly the fainter of the two when both are drawn.
constexpr uint32_t GHOST_ALPHA = 0x9000;
constexpr uint32_t GHOST_DOT = 2;
constexpr uint32_t STATUS_FRAMES = 0x1d, STATUS_FRAMES_LONG = 0x3c, STATUS_FRAMES_BLINK = 0x5a;

// What every part of the hole's drawing needs to know, gathered once a frame. Passed as `h`
// throughout this file; `hole` on its own always means the hole's *number*, 0..17.
struct Hole {
    uint32_t state, steps;
    int32_t course, hole;
    uint32_t pack, small_font, large_font;
    bool wide;
    uint32_t scratch, scratch2;  // SCRATCH_TEXT and DIALOG_MESSAGE
};

const CourseInfo& course_info(const Hole& h) {
    return course_info_at(h.course);
}
int32_t par(const Hole& h, int32_t hole) {
    return course_info(h).pars[hole];
}
int32_t sine(uint32_t angle) {
    return static_cast<int32_t>(guest_array<int16_t>(SINE_TABLE)[angle & 0xff]);
}
int32_t ball_screen_x() {
    return CAMERA_X + (to_whole(play_state().ball_x));
}
int32_t ball_screen_y() {
    return CAMERA_Y + (to_whole(play_state().ball_y));
}

void image_draw_clipped(int32_t x, int32_t y, uint32_t width, uint32_t height, uint32_t u,
                        uint32_t v, ImageRecord& image, Blend blend) {
    image_draw_clipped_at(x, y, static_cast<int32_t>(width), static_cast<int32_t>(height),
                          static_cast<int32_t>(u), static_cast<int32_t>(v), image,
                          static_cast<uint32_t>(blend));
}

void text_draw_outlined(FontRecord& font, uint32_t text, int32_t x, int32_t y, Align align,
                        uint32_t style) {
    text_draw_at(game_state_block().handle, font, text, x, y, align, style);
}

// A number, and a string, appended to a buffer in the current encoding.
void number(const Hole& h, uint32_t buffer, uint32_t value) {
    (h.wide ? wide_number_to_string : number_to_string)(buffer, static_cast<int32_t>(value), 0);
}
void append(const Hole& h, uint32_t destination, uint32_t source) {
    if (h.wide) {
        wide_string_append(destination, source);
    } else {
        string_append(destination, source);
    }
}
void load_text(const Hole& h, uint32_t id, uint32_t destination) {
    resource_load(as_pack(h.pack), id, destination, 0x800);
}

// --- the ground -------------------------------------------------------------------------------

// The perspective quad's eight corner coordinates, as the camera block holds them.
struct Ground {
    uint32_t top_left_y, top_right_y, bottom_right_y, bottom_left_y;  // the "A C G H" column
    uint32_t top_right_x, mid_x, bottom_left_x, bottom_right_x;       // the "B D E F" column
};

Ground ground_corners() {
    const HoleObjects& ground = hole_objects_state();
    const auto column = [&](uint32_t corner) {
        return ground.corners_x[corner] - ground.origin_x + ground.base_x + 0x280'0000;
    };
    const auto row = [&](uint32_t corner) {
        return ground.corners_y[corner] + ground.origin_y + ground.base_y;
    };
    Ground g;
    g.top_left_y = column(2);
    g.top_right_y = column(3);
    g.bottom_right_y = column(1);
    g.bottom_left_y = column(0);
    g.top_right_x = row(3);
    g.mid_x = row(2);
    g.bottom_left_x = row(1);
    g.bottom_right_x = row(0);
    return g;
}

uint32_t wipe_rise(const Hole& h, uint32_t step) {
    const uint32_t index =
        (static_cast<uint32_t>(h.course) * HOLES_PER_COURSE + static_cast<uint32_t>(h.hole)) *
            WIPE_WORDS_PER_HOLE +
        step;
    return GROUND_RISE - guest_array<uint32_t>(WIPE_TABLE)[index];
}

// The wipe (state 30): the perspective quad drawn directly, at the height the table says.
void ground_draw_wipe(const Hole& h, const Ground& g) {
    uint32_t frames = play_state().state_frames;
    if (static_cast<int32_t>(frames) > static_cast<int32_t>(WIPE_LAST)) {
        frames = WIPE_LAST;
    }
    const uint32_t rise = wipe_rise(h, frames) << 16;
    // The whole screen, textured with the ground's four corners (the batcher's pair order).
    const uint32_t xs[4] = {0, to_fixed(SCREEN_WIDTH), to_fixed(SCREEN_WIDTH), 0};
    const uint32_t ys[4] = {0, 0, to_fixed(SCREEN_HEIGHT), to_fixed(SCREEN_HEIGHT)};
    const uint32_t us[4] = {g.bottom_left_y, g.bottom_right_y, g.top_left_y, g.top_right_y};
    const uint32_t vs[4] = {rise - g.bottom_right_x, rise - g.bottom_left_x, rise - g.mid_x,
                            rise - g.top_right_x};
    textured_quad_push(xs, ys, us, vs, hole_objects_state().texture, 0);
    play_state().byte_630 = static_cast<uint8_t>(0);
}

// The first frame: draw the course through the camera and keep the result as a texture.
void ground_capture(const Hole& h, const Ground& g, uint32_t frame_base) {
    const uint32_t rise = wipe_rise(h, WIPE_WORDS_PER_HOLE) << 16;
    const uint32_t colors = frame_base + 0x34, corners = frame_base + 0x14;
    gfx::set_clear_color(0, 0, 0, FLOAT_ONE);
    gfx::clear(gfx::Buffer::Color);
    gl_mode(gfx::Pipeline::Textured);
    camera_matrix_load();
    gfx::bind_texture(gfx::TextureTarget::Texture2D, TEXTURE_CAPTURE);
    libc::memory_copy(colors, QUAD_COLORS, 0x40);  // memcpy
    gfx::set_vertex_array(0, 4, gfx::AttributeType::Fixed, 0, 0, colors);
    gfx::enable_vertex_array(0);
    const uint32_t corner_words[] = {
        g.top_right_y,    rise - g.top_right_x,   g.top_left_y,    rise - g.mid_x,
        g.bottom_right_y, rise - g.bottom_left_x, g.bottom_left_y, rise - g.bottom_right_x};
    for (uint32_t i = 0; i < 8; ++i) {
        guest_array<uint32_t>(corners)[i] = corner_words[i];
    }
    gfx::set_vertex_array(1, 2, gfx::AttributeType::Fixed, 0, 0, corners);
    gfx::enable_vertex_array(1);
    gfx::draw_arrays(gfx::Primitive::Quads, 0, 4);
    gfx::disable_vertex_array(0);
    gfx::disable_vertex_array(1);
    gfx::bind_texture(gfx::TextureTarget::Texture2D, 0);
    (void)gfx::set_pixel_store(gfx::PixelStore::PackAlignment, 1);
    gfx::bind_texture(gfx::TextureTarget::Texture2D, TEXTURE_GROUND);
    gfx::texture_image(gfx::TextureTarget::Texture2D, 0, gfx::PixelFormat::Rgba, SCREEN_WIDTH,
                       SCREEN_HEIGHT, 0, gfx::PixelFormat::Rgba, gfx::PixelType::UnsignedByte,
                       GROUND_PIXELS);
    gfx::copy_texture_image(gfx::TextureTarget::Texture2D, 0, gfx::PixelFormat::Rgba, 0, 0,
                            SCREEN_WIDTH, SCREEN_HEIGHT, 0);
    gfx::bind_texture(gfx::TextureTarget::Texture2D, 0);
    play_state().byte_630 = static_cast<uint8_t>(1);
}

// Every frame but the wipe: the captured ground as one quad.
void ground_draw_captured(uint32_t frame_base) {
    const uint32_t colors = frame_base + 0xa4, corners = frame_base + 0x84;
    gl_mode(gfx::Pipeline::Ground);
    camera_matrix_load();
    gfx::bind_texture(gfx::TextureTarget::Texture2D, TEXTURE_GROUND);
    libc::memory_copy(colors, GROUND_COLORS, 0x40);
    gfx::set_vertex_array(0, 4, gfx::AttributeType::Fixed, 0, 0, colors);
    gfx::enable_vertex_array(0);
    libc::memory_copy(corners, GROUND_CORNERS, 0x20);
    gfx::set_vertex_array(1, 2, gfx::AttributeType::Fixed, 0, 0, corners);
    gfx::enable_vertex_array(1);
    gfx::draw_arrays(gfx::Primitive::Quads, 0, 4);
    gfx::disable_vertex_array(0);
    gfx::disable_vertex_array(1);
    gfx::bind_texture(gfx::TextureTarget::Texture2D, 0);
}

// --- the objects ------------------------------------------------------------------------------

// The small animated objects (the same list the tick steps during fast-forward).
void small_objects_animate(const Hole& h) {
    if (h.state != 3) {
        const uint32_t phase = play_state().animation_phase + 1;
        play_state().animation_phase = phase >= 6 ? 0 : phase;
    }
    const uint32_t count = static_cast<uint32_t>(players_state().object_count);
    for (uint32_t i = 0; i < count; ++i) {
        HoleSprite& object = sprite_at(i);
        const int32_t raw_kind = object.kind;
        const bool high = raw_kind >= 0x1000;
        const int32_t limit = static_cast<int16_t>(high ? players_state().kind_limit_b
                                                        : players_state().kind_limit_a);
        if ((high ? raw_kind - 0x1000 : raw_kind) >= limit) {
            continue;
        }
        const FrameList& frames = sprite_frames(raw_kind, 0x1000);
        if (play_state().animation_phase == 0 && h.state != 3) {
            object.frame = static_cast<int16_t>(object.frame + 1);
        }
        if (object.frame >= frames.count) {
            object.frame = 0;
        }
    }
}

uint32_t hole_object(uint32_t index) {
    return hole_objects_state().table + index * hole_objects::OBJECT_WORDS * 4;
}

// Whether the ball is behind an object that can hide it: a per-course list of kinds, each
// compared against a line in the object's current frame.
void objects_place_ball(const Hole& h) {
    const uint32_t count = hole_objects_state().count;
    if (count >= KIND_LIMIT) {
        assert_trap(0x1800a598u);
    }
    for (uint32_t i = 0; i < count; ++i) {
        HoleObject& object = as_object(hole_object(i));
        const uint32_t k = object.kind;
        const uint32_t frame_at = static_cast<uint32_t>(static_cast<int8_t>(object.frame)) * 4;
        uint32_t threshold;
        if (h.course == 0 && (k == 9 || k == 10)) {
            threshold = 0xa0'0000 - object.screen_y[frame_at];
        } else if ((h.course == 0 && (k == 17 || k == 18)) ||
                   (h.course == 1 && (k == 14 || k == 17 || k == 18 || k == 22 || k == 56)) ||
                   (h.course == 2 && k == 16)) {
            threshold = 0x78'0000 - object.screen_y[frame_at + 3];
        } else {
            continue;
        }
        object.behind_ball = static_cast<int8_t>(
            static_cast<int32_t>(play_state().ball_y) <= static_cast<int32_t>(threshold) ? 1 : 0);
    }
}

// How an object is drawn: plain with its behind-the-ball flag, plain, one of three special
// styles, or not at all. Course and hole single out a few kinds.
enum class ObjectStyle : int32_t {
    Plain = -1,
    Flat = 0,
    Style1 = 1,
    Style2 = 2,
    Style3 = 3,
    Skip = 4
};

ObjectStyle object_style(const Hole& h, uint32_t k) {
    if (h.course == 0) {
        if (k == 17 || k == 18)
            return ObjectStyle::Flat;
        return k == 19 ? ObjectStyle::Style1 : ObjectStyle::Plain;
    }
    if (h.course == 1) {
        if (k == 18 || k == 22)
            return ObjectStyle::Style1;
        if (k == 14)
            return ObjectStyle::Flat;
        if (h.hole == 15) {
            if (k == 24)
                return ObjectStyle::Style3;
            return k == 25 ? ObjectStyle::Skip : ObjectStyle::Plain;
        }
        if (h.hole == 17) {
            if (k == 0)
                return ObjectStyle::Style2;
            if (k == 1)
                return ObjectStyle::Style1;
            return k == 25 ? ObjectStyle::Skip : ObjectStyle::Plain;
        }
        if (h.hole == 16) {
            return k == 25 ? ObjectStyle::Skip : ObjectStyle::Plain;
        }
        return ObjectStyle::Plain;
    }
    if (h.course == 2) {
        return h.hole == 17 && k == 7 ? ObjectStyle::Style2 : ObjectStyle::Plain;
    }
    return ObjectStyle::Plain;
}

// Advance an object's animation by the frame's steps, as its kind prescribes.
void object_animate(const Hole& h, HoleObject& object, ObjectKind& kind_record) {
    if (h.state == 3 || object.timer == 0) {
        return;
    }
    const uint32_t elapsed = object.elapsed + (to_fixed(h.steps));
    object.elapsed = elapsed;
    if (kind_record.trigger != 0 &&
        static_cast<int32_t>(elapsed) >= static_cast<int32_t>(to_fixed(object.wait))) {
        object.triggered = static_cast<uint8_t>(1);
    } else if (static_cast<uint32_t>(object.triggered) != 1) {
        return;
    }
    if (static_cast<int32_t>(elapsed) < static_cast<int32_t>(object.timer)) {
        return;
    }
    object.elapsed = 0;
    const uint32_t count = kind_record.frame_count;
    const int32_t frame = static_cast<int8_t>(static_cast<uint32_t>(object.frame) + 1);
    switch (kind_record.animation) {
    case 0:
        break;
    case 1:  // once, and stay on the last frame
        object.frame = static_cast<uint8_t>(static_cast<uint32_t>(frame));
        if (frame == static_cast<int32_t>(count)) {
            object.frame = static_cast<uint8_t>(count - 1);
        }
        break;
    case 2:  // loop, waiting again between runs if the kind says so
        object.frame = static_cast<uint8_t>(static_cast<uint32_t>(frame));
        if (frame != static_cast<int32_t>(count)) {
            break;
        }
        object.frame = static_cast<uint8_t>(0);
        if (kind_record.trigger == 2) {
            object.triggered = static_cast<uint8_t>(0);
            object.wait = random_next(game_state_block().object_24, kind_record.wait);
        } else if (kind_record.trigger == 1) {
            object.triggered = static_cast<uint8_t>(0);
            object.wait = kind_record.wait;
        }
        break;
    case 3: {  // back and forth
        const int32_t direction = static_cast<int32_t>(static_cast<uint32_t>(object.direction));
        const int32_t next = static_cast<int8_t>(
            static_cast<int32_t>(static_cast<uint32_t>(object.frame)) + direction);
        object.frame = static_cast<uint8_t>(static_cast<uint32_t>(next));
        if (next < 0) {
            object.frame = static_cast<uint8_t>(1);
            object.direction = static_cast<int8_t>(static_cast<uint32_t>(-direction));
        } else if (next == static_cast<int32_t>(count)) {
            object.frame = static_cast<uint8_t>(count - 1);
            object.direction = static_cast<int8_t>(static_cast<uint32_t>(-direction));
        }
        break;
    }
    default:
        assert_trap(0x1800a988u);
    }
}

void objects_draw(const Hole& h) {
    const uint32_t count = hole_objects_state().count;
    if (count >= KIND_LIMIT) {
        assert_trap(0x1800a678u);
    }
    for (uint32_t i = 0; i < count; ++i) {
        HoleObject& object = as_object(hole_object(i));
        const uint32_t k = object.kind;
        ObjectKind& kind_record = as_kind(OBJECT_KINDS + k * OBJECT_KIND_SIZE);
        const int32_t frame = static_cast<int32_t>(static_cast<int8_t>(object.frame));
        const uint32_t mesh_index = kind_record.meshes[static_cast<uint32_t>(frame)];
        ImageRecord& mesh = as_image(MESHES + mesh_index * MESH_SIZE);
        // Note the meshes the player has seen on this hole (the visit table is by course and hole).
        for (uint32_t visit = 0; visit < MESH_VISIT_COUNT; ++visit) {
            const MeshVisit& entry = mesh_visit(visit);
            if (static_cast<int32_t>(entry.course) != h.course ||
                static_cast<int32_t>(entry.hole) != h.hole) {
                continue;
            }
            if (mesh_index == entry.mesh_a || mesh_index == entry.mesh_b) {
                mesh_seen(visit) = 1;
            }
        }
        if (k >= KIND_LIMIT) {
            assert_trap(0x1800a748u);
        }
        if (kind_record.meshes[0] >= MESH_LIMIT) {
            assert_trap(0x1800a758u);
        }
        const ObjectStyle style = object_style(h, k);
        const uint32_t handle = game_state_block().handle;
        if (style == ObjectStyle::Plain) {
            object_draw(handle, object, mesh,
                        static_cast<uint32_t>(object.behind_ball) != 0 ? Blend::Keyed
                                                                       : Blend::Alpha);
        } else if (style == ObjectStyle::Flat) {
            object_draw(handle, object, mesh, Blend::Alpha);
        } else if (style != ObjectStyle::Skip) {
            object_draw(handle, object, mesh, static_cast<Blend>(style));
        }
        object_animate(h, object, kind_record);
    }
}

// --- the ball, the aim, the tee ----------------------------------------------------------------

// Aiming or choosing power: a dotted line along the aim, as far as the meter reaches, the
// dots marching outward with time.
//
// The reach is play::METER_VALUE, which `aim_line_measure` (hole_tick.cpp) sets to the distance
// along the aim before something blocks it — so while aiming, the line shows how far the ball
// could go, and it goes on showing that while the power meter swings. With this port's "aim
// guide" cheat the line follows the meter instead: it is shortened to the same fraction of its
// reach that the meter bar is showing, so the player can see where the shot is aimed *and* how
// hard it is about to be hit, at once. 0x8000 is the meter's full scale, as `power_meter_draw`
// reads it.
void aim_line_draw() {
    const uint32_t angle = play_state().aim_angle;
    const int32_t dx = sine(angle + 0x40) * 2, dy = -(sine(angle) * 2);
    const int32_t x0 = ball_screen_x(), y0 = ball_screen_y();
    const libc::Division division =
        libc::signed_divide(play_state().state_frames >> 2, play_state().word_620);
    int32_t along = static_cast<int32_t>(division.remainder + play_state().word_61c);
    int32_t reach = static_cast<int32_t>(play_state().meter_value);
    if (play_state().state == POWER && cheat_enabled(Cheat::AimGuide)) {
        reach = (reach * power_meter_value()) >> 15;
    }
    for (; along < reach; along += static_cast<int32_t>(play_state().word_620)) {
        const int32_t x = x0 + ((along * dx) >> 16), y = y0 + ((along * dy) >> 16);
        rect_fill(to_fixed(x), to_fixed(y), to_fixed(2), to_fixed(2), WHITE, WHITE, WHITE, WHITE,
                  Blend::Additive);
    }
}

// This port's ghost: the path the ball took on the best round of this hole (round_history.h),
// drawn faintly beneath everything else so a player can see the line that scored it. Course
// coordinates reach the screen the same way the ball's do — the camera is fixed while a hole is
// being played, so `ball_screen_x` is just the camera's offset plus the whole part.
//
// It is drawn only while the player is deciding — placing, aiming, choosing power — and not
// while the ball is rolling, where a second path moving under the first would be one thing too
// many on a 320x240 screen.
void ghost_trail_draw(const Hole& h) {
    if (!cheat_enabled(Cheat::GhostTrail)) {
        return;
    }
    const GhostPath& path =
        hole_ghost(static_cast<uint32_t>(h.course), static_cast<uint32_t>(h.hole));
    for (size_t i = 0; i < path.size(); ++i) {
        const int32_t x = CAMERA_X + static_cast<int32_t>(path.x[i]);
        const int32_t y = CAMERA_Y + static_cast<int32_t>(path.y[i]);
        rect_fill(to_fixed(x), to_fixed(y), to_fixed(GHOST_DOT), to_fixed(GHOST_DOT), WHITE, WHITE,
                  WHITE, GHOST_ALPHA, Blend::Additive);
    }
}

// Placing the ball: a marker under it and four arrows pulsing the way it can move.
void tee_arrows_draw() {
    const int32_t x = ball_screen_x() - 1, y = ball_screen_y() - 1;
    const int32_t pulse = static_cast<int32_t>((play_state().state_frames >> 2) & 3);
    image_draw(x - 2, y - 2, MARKER_SIZE, MARKER_SIZE, as_image(TEE_MARKER), 0, 0, 1,
               Blend::Additive);
    const auto arrow = [&](int32_t ax, int32_t ay) {
        image_draw(ax, ay, ARROW_WIDTH, ARROW_HEIGHT, as_image(SPRITES), 0, 0, 0, Blend::Additive);
    };
    switch (static_cast<uint32_t>(play_state().place_direction)) {
    case 1:  // up and down
        arrow(x, y - pulse * 8);
        arrow(x, y - pulse * 4);
        arrow(x, y + pulse * 8);
        arrow(x, y + pulse * 4);
        break;
    case 2:  // along the diagonal
        arrow(x - pulse * 8, y + pulse * 8);
        arrow(x - pulse * 4, y + pulse * 4);
        arrow(x + pulse * 8, y - pulse * 8);
        arrow(x + pulse * 4, y - pulse * 4);
        break;
    default:  // left and right
        arrow(x - pulse * 8, y);
        arrow(x - pulse * 4, y);
        arrow(x + pulse * 8, y);
        arrow(x + pulse * 4, y);
        break;
    }
}

// The ball: one of four frames as it rolls, or a hint frame while the hint sequence runs.
void ball_draw() {
    const uint32_t hint = static_cast<uint32_t>(play_state().hint);
    uint32_t u;
    if (hint == 0) {
        u = BALL_FRAME_MASK & (play_state().word_644 >> 14);
    } else if (hint < 4) {
        u = (hint + 3) * 4;
    } else {
        return;
    }
    image_draw_clipped(ball_screen_x() - 1, ball_screen_y() - 1, BALL_WIDTH, BALL_HEIGHT, u, 0,
                       as_image(SPRITES), Blend::Additive);
}

// 0x1800e39c — an object's current frame's corners into screen space: each corner through the
// object's own rotation, moved by the object's place relative to the camera, then through the
// view matrix. The results land in the object's records of x (+0) and y (+0x200).
void object_transform(HoleObject& object, uint32_t view_matrix_at) {
    if (view_matrix_at == 0) {
        assert_trap(0x1800e3acu);
    }
    HoleObject& record = object;
    // Both matrices are read out of their records first: an array in a packed overlay cannot
    // portably be passed as a pointer (see matrix_read).
    uint32_t view_matrix[MATRIX_WORDS], object_matrix[MATRIX_WORDS];
    matrix_read(view_matrix_at, view_matrix);
    matrix_read(address_of(record) + static_cast<uint32_t>(offsetof(HoleObject, matrix)),
                object_matrix);
    const uint32_t frame_at = static_cast<uint32_t>(static_cast<int8_t>(object.frame)) * 4;
    for (uint32_t corner = 0; corner < 4; ++corner) {
        const uint32_t at = frame_at + corner;
        uint32_t local[4] = {record.quads[at], record.quads_b[at], 0, 0x10000};
        uint32_t world[4];
        matrix_transform(object_matrix, local, world);
        world[0] = world[0] + record.x - hole_objects_state().base_x;
        world[1] = world[1] - (record.y + hole_objects_state().base_y);
        matrix_transform(view_matrix, world, local);
        record.screen_x[at] = local[0];
        record.screen_y[at] = local[1];
    }
}

// 0x18009d50 / 0x18011654 — one object as a textured quad: its frame's corners transformed
// and put about the screen's centre, the mesh's rectangle of its texture, and the blend the
// caller chose (the first form: keyed when the object is behind the ball, alpha otherwise).
void object_draw(uint32_t handle, HoleObject& object, ImageRecord& mesh, Blend blend) {
    if (handle == 0 || static_cast<uint32_t>(mesh.variant) >= 3) {
        assert_trap(0x18009d68u);
    }
    ImageRecord& texture = as_image(TITLE_IMAGE + mesh.texture_index * TEXTURE_TABLE_STRIDE);
    const uint32_t frame_at = static_cast<uint32_t>(static_cast<int8_t>(object.frame)) * 4;
    object_transform(object, HOLE_OBJECTS + static_cast<uint32_t>(offsetof(HoleObjects, matrix_b)));
    TexturedQuad quad;
    uint32_t* corner_x[4] = {&quad.x0, &quad.x1, &quad.x2, &quad.x3};
    uint32_t* corner_y[4] = {&quad.y0, &quad.y1, &quad.y2, &quad.y3};
    for (uint32_t i = 0; i < 4; ++i) {
        *corner_x[i] = object.screen_x[frame_at + i] + (to_fixed(SCREEN_CENTRE_X));
        *corner_y[i] = (to_fixed(SCREEN_CENTRE_Y)) - object.screen_y[frame_at + i];
    }
    const uint32_t top = texture.height - mesh.v;
    quad.u0 = to_fixed(mesh.u);
    quad.u1 = (mesh.u + mesh.width) << 16;
    quad.v0 = (top - mesh.height) << 16;
    quad.v1 = to_fixed(top);
    quad.alpha = 0x10000;
    quad.texture = texture.texture_name;
    quad.blend = blend;
    textured_quad_draw(quad);
}

// 0x18009a98 — the aim arrow: one of eight sprites by octant of the angle, nudged along the
// octant's direction by where the angle sits inside it, in the current player's colour. The
// short form (a shorter shaft, for the meter) has its own row of sprites for the upward half.
void arrow_draw_at(int32_t x, int32_t y, int32_t camera_x, int32_t camera_y, uint32_t angle,
                   uint32_t short_form) {
    const uint32_t octant = ((angle + 0x10) >> 5) & 7;
    const int32_t within = static_cast<int32_t>(((angle + 0x10) & 0x1f) >> 1) - 8;
    const uint32_t player = static_cast<uint32_t>(players_state().current);
    const int32_t gender = static_cast<int32_t>(player_record(player).gender);
    const bool upward_short = short_form != 0 && angle > 0x47 && angle < 0x80;
    const uint32_t sprite =
        ARROW_SPRITES + static_cast<uint32_t>(gender + (upward_short ? 0x1f : 0x13)) * image::SIZE;
    as_image(sprite).variant = static_cast<uint8_t>(upward_short ? 2 : 1);
    const int32_t origin_x = static_cast<int32_t>(as_image(sprite).origin_x);
    uint32_t width = ARROW_SPRITE_WIDTH,
             u = octant * ARROW_SPRITE_WIDTH - static_cast<uint32_t>(origin_x);
    if (octant == 0) {
        width = ARROW_SPRITE_WIDTH - static_cast<uint32_t>(origin_x);
        u = 0;
    } else if (octant == 7) {
        const int32_t overhang = static_cast<int32_t>(as_image(sprite).word_24) - origin_x -
                                 static_cast<int32_t>(as_image(sprite).cell_u);
        width = ARROW_SPRITE_WIDTH - static_cast<uint32_t>(overhang);
        u = 0xaf;
    }
    const int32_t draw_x =
        camera_x +
        ((within * static_cast<int32_t>(guest_array<uint32_t>(ARROW_STEP_X)[octant]) + x) >> 16) +
        static_cast<int32_t>(guest_array<int8_t>(ARROW_OFFSET_X)[octant]);
    const int32_t draw_y =
        camera_y +
        ((within * static_cast<int32_t>(guest_array<uint32_t>(ARROW_STEP_Y)[octant]) + y) >> 16) +
        static_cast<int32_t>(guest_array<int8_t>(ARROW_OFFSET_Y)[octant]);
    image_draw(draw_x, draw_y, width, as_image(sprite).cell_height, as_image(sprite), u, 0,
               static_cast<uint32_t>(as_image(sprite).variant), Blend::Additive);
}

void arrow_draw(uint32_t x, uint32_t y, bool short_form) {
    arrow_draw_at(static_cast<int32_t>(x), static_cast<int32_t>(y), CAMERA_X, CAMERA_Y,
                  play_state().aim_angle, short_form ? 1u : 0u);
}

void ball_and_arrow_draw(const Hole& h) {
    const uint32_t state = h.state;
    if (state == ROLLING) {  // the spin follows the speed
        const int32_t vx = static_cast<int32_t>(play_state().velocity_x) >> 8;
        const int32_t vy = static_cast<int32_t>(play_state().velocity_y) >> 8;
        play_state().word_644 = play_state().word_644 + static_cast<uint32_t>(vx * vx + vy * vy);
    }
    // The ball is drawn for every state up to the last hint, except while it is dropping in.
    const bool sinking = state == HOLED || state == HOLED_ALT;
    if (!sinking && state <= HINT_3) {
        ball_draw();
    }
    if (state == AIMING || state == POWER || state == STRUCK) {  // the arrow, then the ball on it
        arrow_draw(play_state().ball_x, play_state().ball_y,
                   static_cast<int32_t>(play_state().meter_value) < 0x28);
        image_draw_clipped(ball_screen_x() - 1, ball_screen_y() - 1, BALL_WIDTH, BALL_HEIGHT,
                           BALL_FRAME_MASK & (play_state().word_644 >> 14), 0, as_image(SPRITES),
                           Blend::Additive);
    }
    if (state >= ROLLING && state <= HINT_3) {  // the arrow stays where the stroke began
        arrow_draw(play_state().stroke_start_x, play_state().stroke_start_y, false);
    }
}

// --- the heads-up display ----------------------------------------------------------------------

Slide& layer(uint32_t index) {
    return text_block().slides[index];
}

void hud_draw(const Hole& h) {
    ImageRecord& panel = as_image(layer(0).picture);
    const int32_t panel_x = to_whole(layer(0).x);
    const int32_t panel_y = to_whole(layer(0).y);
    image_draw(panel_x, panel_y, panel.width, panel.height, panel, 0, 0,
               static_cast<uint32_t>(panel.variant), Blend::Additive);
    const int32_t origin_x = panel_x - static_cast<int32_t>(panel.origin_x);
    const int32_t origin_y = panel_y - static_cast<int32_t>(panel.origin_y);

    // The hole's badge, its number over par, and the stroke count.
    image_draw(static_cast<int32_t>(course_info(h).hole_badge_x) + origin_x,
               static_cast<int32_t>(course_info(h).hole_badge_y) + origin_y,
               as_image(HOLE_BADGE).width, as_image(HOLE_BADGE).height, as_image(HOLE_BADGE), 0, 0,
               static_cast<uint32_t>(as_image(HOLE_BADGE).variant), Blend::Keyed);
    load_text(h, TEXT_HOLE_LABEL, h.scratch);
    append(h, h.scratch, LITERAL_NEWLINE);
    number(h, h.scratch2, static_cast<uint32_t>(par(h, h.hole)));
    append(h, h.scratch, h.scratch2);
    text_draw_outlined(as_font(h.large_font), h.scratch,
                       static_cast<int32_t>(course_info(h).hole_text_x) + origin_x,
                       static_cast<int32_t>(course_info(h).hole_text_y) + origin_y, Align::Centre,
                       3);
    const uint32_t player = static_cast<uint32_t>(players_state().current);
    number(h, h.scratch,
           static_cast<uint32_t>(
               static_cast<uint32_t>(static_cast<int16_t>(player_record(player).strokes))));
    text_draw_outlined(as_font(h.small_font), h.scratch,
                       static_cast<int32_t>(course_info(h).strokes_x) + origin_x,
                       static_cast<int32_t>(course_info(h).strokes_y) + origin_y, Align::Left, 3);

    // The status panel the HUD's state points at (the layer index is TEXT + 0x738).
    const Slide& status = layer(text_block().word_738);
    ImageRecord& status_image = as_image(status.picture);
    image_draw(to_whole(status.x), to_whole(status.y), status_image.width, status_image.height,
               status_image, 0, 0, static_cast<uint32_t>(status_image.variant), Blend::Additive);

    // The result badge with the hole number, when the state shows it.
    if (text_block().word_734 == 2 && badge_flags().shown != 0) {
        const int32_t badge_y =
            static_cast<int32_t>(course_info(h).result_badge_y) + (to_whole(layer(1).y));
        const int32_t badge_x = static_cast<int32_t>(SCREEN_WIDTH - as_image(RESULT_BADGE).width);
        image_draw(badge_x, badge_y, as_image(RESULT_BADGE).width, as_image(RESULT_BADGE).height,
                   as_image(RESULT_BADGE), 0, 0,
                   static_cast<uint32_t>(as_image(RESULT_BADGE).variant), Blend::Keyed);
        number(h, h.scratch, static_cast<uint32_t>(h.hole + 1));
        text_draw_outlined(as_font(h.large_font), h.scratch, badge_x + 0x15, badge_y + 0xc,
                           Align::Centre, badge_flags().covered == 0 ? 3 : 4);
    }

    // The status animation: the panels slide in (0), out (1), wait (2, 3), hold (4, 5), blink (6).
    uint32_t counter = text_block().word_73c;
    switch (text_block().word_734) {
    case 0:
        for (uint32_t i = 0; i < 5; ++i) {
            layer(i).y = layer(i).y + layer(i).speed;
        }
        text_block().word_73c = ++counter;
        if (counter == STATUS_FRAMES) {
            text_block().word_734 = 2;
            text_block().word_73c = 0;
        }
        break;
    case 1:
        for (uint32_t i = 0; i < 5; ++i) {
            layer(i).y = layer(i).y - 2 * layer(i).speed;
        }
        break;
    case 2:
        if (static_cast<uint32_t>(text_block().selection) != 7 && (h.state == 1 || h.state == 2)) {
            text_block().word_738 = 1;
            text_block().word_734 = 3;
            text_block().word_73c = 0;
        }
        break;
    case 4:
    case 5:
        text_block().word_73c = ++counter;
        if (counter == STATUS_FRAMES_LONG) {
            text_block().word_738 = 1;
            text_block().word_734 = 3;
            text_block().word_73c = 0;
        }
        break;
    case 6:
        text_block().word_73c = ++counter;
        if (counter == STATUS_FRAMES_BLINK) {
            text_block().word_738 = 1;
            text_block().word_734 = 3;
            text_block().word_73c = 0;
        } else {
            const libc::Division blink = libc::signed_divide(counter, 0x1e);
            text_block().word_738 = static_cast<int32_t>(blink.remainder) >= 0xf ? 1 : 2;
        }
        break;
    default:
        break;
    }
}

// --- the score card ----------------------------------------------------------------------------

// The card the hole shows between holes and the pause menu shows on demand: a row per player
// with a column per hole, the par line above it, and the running total at the right. Nine holes
// fit across the screen, so the card scrolls to the half the current hole is in.
void score_card_draw(const Hole& h) {
    ImageRecord& picture = as_image(text_block().slides[SCORE_CARD_SLIDE].picture);
    const int32_t card_x = to_whole(text_block().slides[SCORE_CARD_SLIDE].x);
    const int32_t card_y = to_whole(text_block().slides[SCORE_CARD_SLIDE].y);
    const uint32_t mode = static_cast<uint32_t>(menu_state().game_mode);
    const bool two_player = mode == 1;
    const uint32_t columns = CARD_COLUMNS + (two_player ? 16 : 0);
    const auto column = [&](uint32_t i) {
        return static_cast<int32_t>(guest_array<uint32_t>(columns)[i]) + card_x;
    };

    image_draw(static_cast<int32_t>(picture.origin_x) + card_x,
               static_cast<int32_t>(picture.origin_y) + card_y, picture.width, picture.height,
               picture, 0, 0, static_cast<uint32_t>(picture.variant), Blend::KeyedAlt);
    const int32_t badge_y =
        static_cast<int32_t>(course_info(h).result_badge_y) + (to_whole(layer(1).y)) + 0xc;
    badge_flags().covered = static_cast<int32_t>(picture.height) + card_y < badge_y ? 1 : 0;

    // The scroll arrows and the scrollbar.
    image_draw(card_x + CARD_SCROLL_X, card_y + CARD_ARROW_UP_Y, as_image(ARROW_UP_IMAGE).width,
               as_image(ARROW_UP_IMAGE).height, as_image(ARROW_UP_IMAGE), 0, 0,
               static_cast<uint32_t>(as_image(ARROW_UP_IMAGE).variant), Blend::KeyedAlt);
    image_draw(card_x + CARD_SCROLL_X, card_y + CARD_ARROW_DOWN_Y, as_image(ARROW_DOWN_IMAGE).width,
               as_image(ARROW_DOWN_IMAGE).height, as_image(ARROW_DOWN_IMAGE), 0, 0,
               static_cast<uint32_t>(as_image(ARROW_DOWN_IMAGE).variant), Blend::KeyedAlt);
    uint32_t bar_height = 0x5d0000, bar_y = 0x530000;
    if (mode != 2) {
        const int64_t cursor = static_cast<int32_t>(static_cast<uint32_t>(menu_state().cursor))
                               << 16;
        bar_y += static_cast<uint32_t>((cursor * 0x52aaa) >> 16);
        bar_height = 0x2e8000;
    }
    const uint32_t arrow_width = as_image(ARROW_UP_IMAGE).width;
    rect_fill(to_fixed(card_x + CARD_SCROLL_X), bar_y + (to_fixed(card_y)), to_fixed(arrow_width),
              bar_height, 0, 0, 0, FULL, Blend::KeyedAlt);
    rect_fill(to_fixed(card_x + CARD_SCROLL_X + 1), bar_y + (to_fixed(card_y + 1)),
              (arrow_width - 2) << 16, bar_height - 0x20000, WHITE, WHITE, WHITE, WHITE,
              Blend::KeyedAlt);

    // The title row and the column headings.
    const int32_t title_y = card_y + (h.wide ? 0x15 : 0x10),
                  heading_y = card_y + (h.wide ? 0x2f : 0x2d);
    load_text(h, TEXT_CARD_TITLE, h.scratch);
    text_draw(as_font(h.small_font), h.scratch, static_cast<int32_t>(SCREEN_CENTRE_X), title_y,
              Align::Centre);
    load_text(h, TEXT_CARD_HOLE, h.scratch);
    text_draw(as_font(h.large_font), h.scratch, column(0), heading_y, Align::Centre);
    load_text(h, TEXT_CARD_PAR, h.scratch);
    text_draw(as_font(h.large_font), h.scratch, column(1), heading_y, Align::Centre);
    load_text(h, two_player ? TEXT_CARD_P1 : TEXT_CARD_STROKES, h.scratch);
    text_draw(as_font(h.large_font), h.scratch, column(2), heading_y, Align::Centre);
    if (two_player) {
        load_text(h, TEXT_CARD_P2, h.scratch);
        text_draw(as_font(h.large_font), h.scratch, column(3), heading_y, Align::Centre);
    }

    // One row per hole in view: number, par, each player's strokes ("-" before it is played).
    int32_t row_y = card_y + 0x3e;
    const int32_t first = static_cast<int32_t>(static_cast<uint32_t>(menu_state().cursor));
    const int32_t last =
        first + static_cast<int32_t>(static_cast<uint32_t>(menu_state().visible_rows));
    for (int32_t hole = first; hole < last; ++hole, row_y += static_cast<int32_t>(CARD_ROW_PITCH)) {
        number(h, h.scratch, static_cast<uint32_t>(hole + 1));
        text_draw(as_font(h.large_font), h.scratch, column(0), row_y, Align::Centre);
        number(h, h.scratch, static_cast<uint32_t>(par(h, hole)));
        text_draw(as_font(h.large_font), h.scratch, column(1), row_y, Align::Centre);
        const auto strokes_cell = [&](const PlayerRecord& player) {
            const int32_t strokes = player.strokes_by_hole[hole];
            if (strokes != 0) {
                number(h, h.scratch, static_cast<uint32_t>(strokes));
            } else {
                if (h.wide) {
                    wide_string_copy(h.scratch, LITERAL_DASH);
                } else {
                    string_copy(h.scratch, LITERAL_DASH);
                }
            }
        };
        strokes_cell(player_record(0));
        text_draw(as_font(h.large_font), h.scratch, column(2), row_y, Align::Centre);
        if (two_player) {
            strokes_cell(player_record(1));
            text_draw(as_font(h.large_font), h.scratch, column(3), row_y, Align::Centre);
        }
    }
    if (mode == 2) {
        return;
    }

    // The totals row: par so far, and each player's strokes with the difference to that par.
    int32_t par_played = 0, player1_played = 0, player2_played = 0;
    for (int32_t hole = 0; hole < static_cast<int32_t>(HOLES_PER_COURSE); ++hole) {
        const int32_t p1 = static_cast<int32_t>(
            static_cast<uint32_t>(player_record(0).strokes_by_hole[static_cast<uint32_t>(hole)]));
        if (p1 == 0) {
            break;
        }
        player1_played += p1;
        // Player 1's record is player 0's plus one stride, which is what indexing past the end
        // of player 0's row amounted to; saying so is both clearer and in bounds.
        const int32_t p2 = static_cast<int32_t>(
            static_cast<uint32_t>(player_record(1).strokes_by_hole[static_cast<uint32_t>(hole)]));
        player2_played += p2;
        par_played += par(h, hole);
    }
    const int32_t total_y = card_y + (h.wide ? 0xc0 : 0xbf);
    load_text(h, TEXT_CARD_TOTAL, h.scratch);
    text_draw(as_font(h.large_font), h.scratch, card_x + 0x5f, total_y, Align::Right);
    number(h, h.scratch, text_block().total_par);
    text_draw(as_font(h.large_font), h.scratch, column(1), total_y, Align::Centre);
    const auto with_difference = [&](uint32_t total, int32_t difference) {
        number(h, h.scratch, total);
        if (!two_player) {
            append(h, h.scratch, LITERAL_SPACE);
            append(h, h.scratch, LITERAL_OPEN);
            if (difference > 0) {
                append(h, h.scratch, LITERAL_PLUS);
            }
            number(h, h.scratch2, static_cast<uint32_t>(difference));
            append(h, h.scratch, h.scratch2);
            append(h, h.scratch, LITERAL_CLOSE);
        }
    };
    with_difference(text_block().total_player1, player1_played - par_played);
    text_draw(as_font(h.large_font), h.scratch, column(2), total_y, Align::Centre);
    if (two_player) {
        with_difference(text_block().total_player2, player2_played - par_played);
        text_draw(as_font(h.large_font), h.scratch, column(3), total_y, Align::Centre);
    }
}

// --- the rest ----------------------------------------------------------------------------------

// The picture of the course just unlocked (states 27-29), with its four lines of text.
void unlocked_picture_draw(const Hole& h) {
    ImageRecord& picture = as_image(text_block().slides[COURSE_PICTURE_SLIDE].picture);
    const int32_t x = to_whole(text_block().slides[COURSE_PICTURE_SLIDE].x);
    const int32_t y = to_whole(text_block().slides[COURSE_PICTURE_SLIDE].y);
    image_draw(static_cast<int32_t>(picture.origin_x) + x,
               static_cast<int32_t>(picture.origin_y) + y, picture.width, picture.height, picture,
               0, 0, static_cast<uint32_t>(picture.variant), Blend::KeyedAlt);
    const int32_t badge_y =
        static_cast<int32_t>(course_info(h).result_badge_y) + (to_whole(layer(1).y)) + 0xc;
    badge_flags().covered = static_cast<int32_t>(picture.height) + y < badge_y ? 1 : 0;
    const struct {
        uint32_t text, draw_return;
        int32_t dy;
        bool small;
    } lines[] = {
        {TEXT_UNLOCK_1, 0x1800c00cu, 0x17, true},
        {TEXT_UNLOCK_2, 0x1800c040u, 0x4a, false},
        {TEXT_UNLOCK_3, 0x1800c074u, 0x77, false},
        {TEXT_UNLOCK_4, 0x1800c0a8u, 0xce, false},
    };
    for (const auto& line : lines) {
        load_text(h, line.text, h.scratch);
        text_draw(as_font(line.small ? h.small_font : h.large_font), h.scratch,
                  static_cast<int32_t>(SCREEN_CENTRE_X), y + line.dy, Align::Centre);
    }
}

// The power meter (state 3): the bar filling to the meter's reading, and the marker above it.
void power_meter_draw() {
    const int32_t width = static_cast<int32_t>(as_image(METER_IMAGE).width);
    const int32_t half_height = halve(static_cast<int32_t>(as_image(METER_IMAGE).height));
    const int32_t empty = width - ((width * power_meter_value()) >> 15);
    const int32_t x0 = halve(static_cast<int32_t>(SCREEN_WIDTH) - width);
    const int32_t y0 = static_cast<int32_t>(SCREEN_HEIGHT) - half_height - 2;
    image_draw_clipped(x0, y0, static_cast<uint32_t>(width - empty),
                       static_cast<uint32_t>(half_height), 0, 0, as_image(METER_IMAGE),
                       Blend::KeyedAlt);
    image_draw_clipped(x0 + width - empty, y0, static_cast<uint32_t>(empty),
                       static_cast<uint32_t>(half_height), static_cast<uint32_t>(width - empty),
                       static_cast<uint32_t>(half_height), as_image(METER_IMAGE), Blend::KeyedAlt);
    image_draw_clipped(x0 + width - empty - 4,
                       static_cast<int32_t>(SCREEN_HEIGHT) -
                           static_cast<int32_t>(as_image(HOLE_BADGE).height) - 3,
                       as_image(HOLE_BADGE).width, as_image(HOLE_BADGE).height, 0, 0,
                       as_image(HOLE_BADGE), Blend::KeyedAlt);
}

}  // namespace

// 0x1800a080 — draw the hole. Answers 1.
uint32_t hole_render() {
    GuestScratch frame(4 * 9 + 0x124);
    Hole h;
    h.state = play_state().state;
    h.steps = play_state().steps_this_frame;
    h.course = static_cast<int32_t>(menu_state().course);
    h.hole = static_cast<int32_t>(menu_state().hole);
    h.pack = game_state_block().pack_handle;
    h.small_font = screen_state().font_object;
    h.large_font = screen_state().text_layout;
    h.wide = static_cast<uint32_t>(menu_state().language) == LANGUAGE_WIDE;
    h.scratch = SCRATCH_TEXT;
    h.scratch2 = DIALOG_MESSAGE;

    const Ground ground = ground_corners();
    if (h.state == 0x1e) {
        ground_draw_wipe(h, ground);
    } else {
        if (static_cast<uint32_t>(play_state().byte_630) == 0) {
            ground_capture(h, ground, registers().r[SP]);
        }
        ground_draw_captured(registers().r[SP]);
    }
    small_objects_animate(h);
    objects_place_ball(h);
    objects_draw(h);

    if (h.state == PLACING || h.state == AIMING || h.state == POWER) {
        ghost_trail_draw(h);
    }
    if (h.state == 2 || h.state == 3) {
        aim_line_draw();
    }
    if (h.state == 1) {
        tee_arrows_draw();
    }
    ball_and_arrow_draw(h);
    hud_draw(h);
    if (h.state >= 0x15 && h.state <= 0x17) {
        score_card_draw(h);
    }
    if (h.state >= 0x1b && h.state <= 0x1d) {
        unlocked_picture_draw(h);
    }
    if (h.state == 0x13 || h.state == 0x11 || h.state == 0xf || h.state == 0x1f) {
        panel_message_draw(0, 1);
    }
    if (h.state == 3) {
        power_meter_draw();
    }
    if (h.state == 0x19) {
        panel_message_draw(1, 0);
    }
    return 1;
}

// 0x180109cc — the panel message: the screen dimmed, a panel sized to the lines it shows
// (with a title line above them when `titled`), growing in over its first frames, then the
// lines centred one under another. The small font is used when `small` is set.
void panel_message_draw(uint32_t titled, uint32_t small) {
    TextBlock& text = as_text(GAME_STATE + game_state::TEXT);
    FontRecord& small_font = as_font(screen_state().font_object);
    FontRecord& font = small != 0 ? small_font : as_font(screen_state().text_layout);
    const int32_t lines = static_cast<int32_t>(static_cast<uint32_t>(text.carousel_count));
    const int32_t shown = static_cast<int32_t>(static_cast<uint32_t>(text.score_card_rows_shown));
    const int32_t rows = lines <= shown ? lines : shown;
    int32_t height = rows * static_cast<int32_t>(font.line_height) + 0x14;
    if (titled != 0) {
        height += static_cast<int32_t>(small_font.line_height) + 0xa;
    }
    const int32_t top = (static_cast<int32_t>(SCREEN_HEIGHT) - height) / 2;
    rect_fill(0, 0, to_fixed(SCREEN_WIDTH), to_fixed(SCREEN_HEIGHT), 0, 0, 0, 0x8000,
              Blend::KeyedAlt);
    if (static_cast<uint32_t>(play_state().panel_growing) != 0) {
        // Shrinking past nothing (never in practice) also tells the loader the panel is gone.
        const bool emptied =
            static_cast<int32_t>(play_state().panel_scale + play_state().panel_scale_step) < 0;
        panel_scale_step();
        if (emptied) {
            course_loader().dialog_shown = 1;
        }
        panel_draw_scaled(0xa, top, 0x12c, height);
        return;
    }
    panel_draw(0xa, top, 0x12c, height);
    int32_t y = top + 0xa;
    if (titled != 0) {
        resource_load(as_pack((game_state_block().pack_handle)), TEXT_PANEL_TITLE, SCRATCH_TEXT,
                      0x800);
        text_draw(small_font, SCRATCH_TEXT, SCREEN_CENTRE_X, y, Align::Centre);
        y += static_cast<int32_t>(small_font.line_height) + 0xa;
    }
    const int32_t first = static_cast<int32_t>(static_cast<uint32_t>(text.carousel_course));
    for (int32_t line = first; line < first + rows; ++line) {
        const uint32_t start = guest_array<uint32_t>(TEXT_LAYOUT_OUT)[line];
        const uint32_t length = guest_array<uint32_t>(TEXT_LAYOUT_OUT)[line + 1] - start;
        if (static_cast<uint32_t>(menu_state().language) == LANGUAGE_WIDE) {
            guest<uint16_t>(DIALOG_MESSAGE) = static_cast<uint16_t>(0);
            wide_string_copy_n(DIALOG_MESSAGE, DIALOG_TEXT + start * 2, length);
            guest<uint16_t>(DIALOG_MESSAGE + length * 2) = static_cast<uint16_t>(0);
        } else {
            guest<uint8_t>(DIALOG_MESSAGE) = static_cast<uint8_t>(0);
            string_copy_n(DIALOG_MESSAGE, DIALOG_TEXT + start, length);
            guest<uint8_t>(DIALOG_MESSAGE + length) = static_cast<uint8_t>(0);
        }
        text_draw(font, DIALOG_MESSAGE, SCREEN_CENTRE_X, y, Align::Centre);
        y += static_cast<int32_t>(font.line_height);
    }
}

// 0x18009fb8 — show a message on the panel: the text is laid out into lines for the panel's
// width and the panel starts growing from nothing.
void panel_message_show(uint32_t text_id) {
    course_loader().dialog_shown = 0;
    play_state().panel_growing = static_cast<uint8_t>(1);
    play_state().panel_scale = 0;
    play_state().panel_scale_step = PANEL_SCALE_STEP_VALUE;
    if (text_id == 0xffffffffu) {
        assert_trap(0x18009fecu);
    }
    resource_load(as_pack(game_state_block().pack_handle), text_id, DIALOG_TEXT, 0x800);
    FontRecord& font = as_font(screen_state().font_object);
    const uint32_t lines =
        text_layout(font, DIALOG_TEXT, TEXT_LAYOUT_OUT, TEXT_LAYOUT_LINES, TEXT_LAYOUT_WIDTH);
    if (lines == 0) {
        assert_trap(0x1800a030u);
    }
    TextBlock& text = as_text(GAME_STATE + game_state::TEXT);
    text.score_card_first = static_cast<uint8_t>(0);
    text.carousel_course = static_cast<int8_t>(0);
    text.carousel_count = static_cast<uint8_t>(static_cast<int8_t>(lines));
    const uint32_t line_height = font.line_height;
    const libc::Division rows =
        libc::signed_divide(SCREEN_HEIGHT - line_height - 0x58, line_height);
    text.score_card_rows_shown = static_cast<uint8_t>(static_cast<int8_t>(rows.quotient));
}

}  // namespace minigolf::game
