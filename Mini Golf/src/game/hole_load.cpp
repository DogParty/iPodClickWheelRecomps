// Setting a hole up from its layout resource: the tile map rasterised from the tee edges and
// obstacles, the animated objects' sprite sheets, the 3-D objects with their meshes and
// textures, the hole's fixed images, and the ball on the tee.
//
// Long because loading a hole is one pass over one resource, and each section of it feeds the
// next. `course_data_load` in particular is the file's own table of contents.
#include "hole_load.h"

#include "calling.h"
#include "draw.h"
#include "fixed.h"
#include "game_state.h"
#include "libc.h"
#include "physics.h"
#include "random.h"
#include "records.h"
#include "resources.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "state.h"

#include <algorithm>
#include <cstring>

namespace minigolf::game {

void ball_to_tee();
int32_t sine_degrees(int32_t degrees);
void ground_camera_prepare(uint32_t first);

namespace {

constexpr uint32_t MAP_SIDE = 0x100;
constexpr uint32_t TILE_OUT = 1, TILE_GREEN = 3, TILE_TEE = 4;
constexpr uint32_t TILE_OBSTACLE_FIRST = 0x40, TILE_SLOPE_FIRST = 0x80;
constexpr uint32_t RECIPROCALS = GAME_STATE + 0x8551a;  // halfword 1/n in 1.15 per row count
constexpr uint32_t HALF_ONE = 0x8000;                   // 1.0 in 1.15
constexpr uint32_t PLACE_NARROW_TEE = 0x14;

// The object kinds (OBJECT_KINDS, 0x94 bytes each) and the objects (HOLE_OBJECTS table).
constexpr uint32_t OBJECT_KINDS = GAME_STATE + 0x857c0, OBJECT_KIND_SIZE = 0x94, KIND_LIMIT = 0x46;
constexpr uint32_t MESHES = GAME_STATE + 0x8b944, MESH_SIZE = 60, MESH_LIMIT = 0x100;
constexpr uint32_t OBJECT_SIZE = 0x864, FRAME_QUADS = 0x20;
constexpr uint32_t OBJECT_SOURCE_SIZE = 0x30;
constexpr uint32_t FRAMES_PER_SECOND = 0x1e;
constexpr uint32_t SHEET_SIZE = 0x48;
constexpr uint32_t GROUND_Y_LIMIT = 0x1e0'0000;
constexpr uint32_t LAYOUT_SECTIONS = 0x904;  // the course data's list of sized sections
constexpr uint32_t LAYOUT_OBJECTS = 0xb70;   // the object entries in the hole's layout
constexpr uint32_t HOLE_DATA_SIZE = 0x1800, HOLE_LAYOUT_SIZE = 0x2000;
// The ground: a grid of 80×60 tiles from the course pack, assembled into one texture.
constexpr uint32_t GROUND_TILE_WIDTH = 0x50, GROUND_TILE_HEIGHT = 0x3c, GROUND_TILE_COUNT = 0x140;
constexpr uint32_t GROUND_TEXTURE_WIDTH = 0x2d0, GROUND_TEXTURE_HEIGHT = 0x258;
constexpr uint32_t GROUND_PALETTE_BYTES = 0x400, GROUND_TILES_STORE = 0x1805'a5e4,
                   TEXTURE_GROUND = 9;
constexpr uint32_t IMAGE_TEXTURES_BEFORE = 6, IMAGE_TEXTURES_FIXED = 2;

uint8_t* tile_map() {
    return guest_array<uint8_t>(guest<uint32_t>(TILE_MAP));
}
void tile_set(uint32_t x, uint32_t y, uint32_t tile) {
    tile_at(x, y) = static_cast<uint8_t>(tile);
}
uint32_t course_pack() {
    return game_state_block().pack_course[static_cast<uint32_t>(menu_state().course)];
}
CourseInfo& course_record() {
    return course_info_at(menu_state().course);
}

}  // namespace

// 0x18015300 — a line of tiles from (x0, y0) to (x1, y1), one per row, stepping x by a
// reciprocal from the table. The tile written is `kind_down` going down the map and `kind_up`
// going up; kind 2 (the rough's edge) also marks the tile before the step. The ends are set
// separately so a one-row line is not lost.
void line_rasterize(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t kind_down,
                    uint32_t kind_up) {
    if (y0 == y1) {
        return;
    }
    uint32_t tile = kind_up, leading = kind_down;
    if (y1 - y0 < 0) {
        tile = kind_down;
        leading = kind_up;
    }
    const bool edge_of_rough = leading == 2;
    if (x1 < x0) {
        const int32_t sx = x0, sy = y0;
        x0 = x1;
        y0 = y1;
        x1 = sx;
        y1 = sy;
    }
    const int32_t direction = y0 >= y1 ? -1 : 1;
    const int32_t rows = (y1 - y0) * direction;
    const uint32_t step =
        guest_array<uint16_t>(RECIPROCALS)[rows] * static_cast<uint32_t>(x1 - x0 + 1);
    uint32_t x = to_fixed(x0);
    for (int32_t y = y0; direction == 1 ? y <= y1 : y >= y1; y += direction, x += step) {
        if (static_cast<uint32_t>(y) >= MAP_SIDE || (x >> 16) >= MAP_SIDE) {
            continue;
        }
        if (edge_of_rough) {
            tile_set(x >> 16, static_cast<uint32_t>(y), 2);
            tile_set((x + step) >> 16, static_cast<uint32_t>(y), tile);
        } else {
            tile_set(x >> 16, static_cast<uint32_t>(y), tile);
        }
    }
    if (static_cast<uint32_t>(x0) < MAP_SIDE && static_cast<uint32_t>(y0) < MAP_SIDE) {
        tile_set(static_cast<uint32_t>(x0), static_cast<uint32_t>(y0), tile);
    }
    if (static_cast<uint32_t>(x1) < MAP_SIDE && static_cast<uint32_t>(y1) < MAP_SIDE) {
        tile_set(static_cast<uint32_t>(x1), static_cast<uint32_t>(y1), tile);
    }
}

// 0x18009588 — a curved edge: a cubic Bézier between the edge's two points with its control
// offsets, drawn as `segments` straight pieces.
void curve_rasterize(const TeeEdge& edge, int32_t segments) {
    const libc::Division division = libc::signed_divide(HALF_ONE, static_cast<uint32_t>(segments));
    const int32_t t_step = static_cast<int32_t>(division.quotient);
    const TeePoint &a = tee_point_at(static_cast<uint32_t>(edge.a)),
                   &b = tee_point_at(static_cast<uint32_t>(edge.b));
    const int32_t x0 = a.x, y0 = a.y, x1 = b.x, y1 = b.y;
    const int32_t c0x = x0 + edge.control[0], c0y = y0 + edge.control[1];
    const int32_t c1x = x1 + edge.control[2], c1y = y1 + edge.control[3];
    const uint32_t kind_down = edge.kind_down, kind_up = edge.kind_up;
    int32_t t = 0, s = static_cast<int32_t>(HALF_ONE);
    int32_t from_x = x0, from_y = y0;
    for (int32_t i = 1; i < segments; ++i) {
        t += t_step;
        s -= t_step;
        const int32_t tt = (t * t) >> 15, tts = (tt * s) >> 15, ttt = (tt * t) >> 15;
        const int32_t st = (s * t) >> 15, sst = (s * st) >> 15, sss = (s * ((s * s) >> 15)) >> 15;
        const int32_t x = ((ttt * x1 + 3 * tts * c1x + 3 * sst * c0x + sss * x0) << 1) >> 16;
        const int32_t y = ((ttt * y1 + 3 * tts * c1y + 3 * sst * c0y + sss * y0) << 1) >> 16;
        line_rasterize(from_x, from_y, x, y, kind_down, kind_up);
        from_x = x;
        from_y = y;
    }
    line_rasterize(from_x, from_y, x1, y1, kind_down, kind_up);
}

// 0x18015258 — one tee edge onto the tile map: straight between its points unless it has
// control offsets, when it curves.
void edge_rasterize(const TeeEdge& edge) {
    const int32_t a = edge.a, b = edge.b;
    if (a == b) {
        return;
    }
    if (edge.control[0] != 0 || edge.control[2] != 0 || edge.control[1] != 0 ||
        edge.control[3] != 0) {
        curve_rasterize(edge, edge.steps);
        return;
    }
    const TeePoint &pa = tee_point_at(static_cast<uint32_t>(a)),
                   &pb = tee_point_at(static_cast<uint32_t>(b));
    line_rasterize(pa.x, pa.y, pb.x, pb.y, edge.kind_down, edge.kind_up);
}

// 0x1800de88 — the hole's tile map: the edges rasterised onto an empty map, every untouched
// tile taking the value of the last edge to its left; then the tee's width chooses how the
// ball is placed (a narrow tee places it along the other axis; course 2's holes 3 and 12 use
// the third way), and each obstacle stamps its tile over the green and slopes around it.
void tile_map_build() {
    libc::memory_clear(guest<uint32_t>(TILE_MAP), MAP_SIDE * MAP_SIDE);
    for (uint32_t y = 0; y < MAP_SIDE; ++y) {
        tile_set(0, y, TILE_OUT);
    }
    const int32_t edges = static_cast<int16_t>(players_state().tee_edge_count);
    for (int32_t i = 0; i < edges; ++i) {
        edge_rasterize(tee_edge_at(static_cast<uint32_t>(i)));
    }
    uint32_t last = TILE_OUT;
    for (uint32_t i = 0; i < MAP_SIDE * MAP_SIDE; ++i) {
        const uint32_t tile = tile_map()[i];
        if (tile == 0) {
            tile_map()[i] = static_cast<uint8_t>(last);
        } else {
            last = tile;
        }
    }
    for (uint32_t i = 0; i < MAP_SIDE * MAP_SIDE; ++i) {
        if (tile_map()[i] != TILE_TEE) {
            continue;
        }
        const uint32_t start = i & 0xff;
        while (tile_map()[i] == TILE_TEE) {
            ++i;
        }
        const uint32_t width = (i & 0xff) - start;
        play_state().place_direction = static_cast<uint8_t>(width >= PLACE_NARROW_TEE ? 0 : 1);
        if (static_cast<uint32_t>(menu_state().course) == 2) {
            const int32_t hole = menu_state().hole;
            if (hole == 3 || hole == 0xc) {
                play_state().place_direction = static_cast<uint8_t>(2);
            } else if (hole == 0xd || hole == 0xe) {
                play_state().place_direction = static_cast<uint8_t>(0);
            }
        }
        break;
    }
    const auto stampable = [](uint32_t tile) {
        return tile == TILE_GREEN || tile >= TILE_SLOPE_FIRST;
    };
    const int32_t obstacles = static_cast<int16_t>(players_state().obstacle_count);
    for (int32_t i = 0; i < obstacles; ++i) {
        const Obstacle& obstacle = obstacle_at(static_cast<uint32_t>(i));
        const uint32_t tile = (TILE_OBSTACLE_FIRST + static_cast<uint32_t>(i)) & 0xff;
        const int32_t ox = obstacle.x, oy = obstacle.y;
        const uint32_t kind = obstacle.kind;
        int32_t width = 0, height = 0;
        switch (kind) {
        case 1:
        case 10:
        case 11:
        case 12:
        case 13:  // cups: a 10×10 square around the centre, with no bounds check
            for (int32_t y = oy - 2; y <= oy + 7; ++y) {
                for (int32_t x = ox - 2; x <= ox + 7; ++x) {
                    uint8_t& at = tile_map()[static_cast<uint32_t>(x + (y << 8))];
                    if (stampable(at)) {
                        at = static_cast<uint8_t>(tile);
                    }
                }
            }
            break;
        case 2:
            width = height = 10;
            break;
        case 3:
            width = height = 14;
            break;
        case 4:
            width = 0x2b;
            height = 0x32;
            break;
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
            width = height = 16;
            break;
        default:
            break;
        }
        for (int32_t y = oy; y < oy + height; ++y) {
            for (int32_t x = ox; x < ox + width; ++x) {
                uint8_t& at = tile_map()[static_cast<uint32_t>(x + (y << 8))];
                if (stampable(at) && static_cast<uint32_t>(x) < MAP_SIDE &&
                    static_cast<uint32_t>(y) < MAP_SIDE) {
                    at = static_cast<uint8_t>(tile);
                }
            }
        }
    }
}

// 0x18010280 — the animated objects' sprite sheets from the layout's section: one 0x48-byte
// entry per sheet (its size, its data, and its image loaded from the course pack), skipping
// sheets no object's frames refer to.
// `count_at` and `table_at` are guest addresses of where the count and the table pointer go:
// both are fields of packed overlays, and a reference to one of those is not portable.
void sprite_sheets_load(Section& section, uint32_t count_at, uint32_t table_at, uint32_t first_id) {
    const int32_t count = static_cast<int32_t>(section.count);
    const uint32_t table = tracked_allocate(static_cast<uint32_t>(count) * SHEET_SIZE + 1);
    if (table == 0) {
        assert_trap(0x180102c0u);
    }
    libc::memory_clear(table, static_cast<uint32_t>(count) * SHEET_SIZE + 1);
    uint32_t offset = 8;
    for (int32_t i = 0; i < count; ++i) {
        const SheetSource& source = as_sheet_source(address_of(section) + offset);
        const uint32_t length = source.length;
        const uint32_t entry = table + static_cast<uint32_t>(i) * SHEET_SIZE;
        SpriteSheet& sheet = as_sheet(entry);
        // Does any object's frame list name this sheet?
        bool used = false;
        const int32_t objects = static_cast<int16_t>(players_state().object_count);
        for (int32_t o = 0; o < objects && !used; ++o) {
            const int32_t kind = sprite_at(static_cast<uint32_t>(o)).kind;
            const bool second = kind >= 0x800;
            const int32_t index = second ? kind - 0x800 : kind;
            if (index >= static_cast<int16_t>(second ? players_state().kind_limit_b
                                                     : players_state().kind_limit_a)) {
                continue;
            }
            const FrameList& frames = sprite_frames(kind, 0x800);
            const int32_t frame_count = frames.count;
            for (int32_t f = 0; f < frame_count; ++f) {
                if ((frames.frames[f] & 0x3ff) == static_cast<uint32_t>(i) + first_id) {
                    used = true;
                    break;
                }
            }
        }
        if (!used) {
            sheet.word_4 = 0;
            offset += length;
            continue;
        }
        sheet.width = source.width;
        sheet.height = source.height;
        sheet.data = address_of(section) + offset + 7;
        image_apply(as_image(entry + 0xc), 1, as_pack(course_pack()),
                    (course_record().sheets + static_cast<uint32_t>(i) + first_id));
        sheet.image.texture_index = sheet.image.texture_index + IMAGE_TEXTURES_BEFORE;
        offset += length;
    }
    guest<uint16_t>(count_at) = static_cast<uint16_t>(count);
    guest<uint32_t>(table_at) = table;
}

// 0x1801197c — the hole's 3-D objects from the layout's entries: each object's record takes
// the entry's quads, position and angle; the meshes their kinds animate through have their
// textures loaded; then every object gets its rotation matrix, its quads moved to where each
// mesh sits in its texture, and its animation timer.
void hole_objects_prepare() {
    GuestScratch frame(4 * 9 + 0x264);
    const int32_t hole = menu_state().hole;
    if (hole >= 0x12 || hole < 0) {
        assert_trap(0x18011994u);
    }
    const uint32_t first_texture = frame.at(0x14c), kind_used = frame.at(0x104),
                   texture_used = frame.at(4);
    libc::memory_fill(first_texture, KIND_LIMIT * 4, 0xffffffffu);
    libc::memory_clear(kind_used, 0x48);
    libc::memory_clear(texture_used, MESH_LIMIT);
    const uint32_t count = hole_objects_state().count;
    hole_objects_state().bytes = count * OBJECT_SIZE;
    const uint32_t table = tracked_allocate(count * OBJECT_SIZE);
    hole_objects_state().table = table;
    if (table == 0) {
        assert_trap(0x180119f4u);
    }
    libc::memory_clear(table, count * OBJECT_SIZE);

    uint32_t source = hole_objects_state().source;
    for (uint32_t o = 0; o < count; ++o, source += OBJECT_SOURCE_SIZE) {
        const uint32_t record = table + o * OBJECT_SIZE;
        if (source == 0) {
            assert_trap(0x18011a28u);
        }
        const ObjectSource& from = as_source(source);
        HoleObject& object = as_object(record);
        for (uint32_t q = 0; q < FRAME_QUADS; ++q) {
            for (uint32_t w = 0; w < 4; ++w) {
                object.quads[q * 4 + w] = from.quad[w];
                object.quads_b[q * 4 + w] = from.quad_b[w];
            }
        }
        object.x = from.x;
        object.y = from.y;
        object.angle = static_cast<uint32_t>(static_cast<int32_t>(from.angle));
        object.kind = static_cast<uint32_t>(static_cast<int32_t>(from.kind));
        object.byte_850 = from.byte_2c;
        object.behind_ball = from.behind_ball;
        object.first_frame = static_cast<uint16_t>(static_cast<uint32_t>(from.first_frame));
        guest_array<uint8_t>(kind_used)[from.kind] = 1;
    }

    for (uint32_t k = 0; k < KIND_LIMIT; ++k) {
        if (guest_array<uint8_t>(kind_used)[k] != 1) {
            continue;
        }
        const uint32_t kind_at = OBJECT_KINDS + k * OBJECT_KIND_SIZE;
        const int32_t frames = static_cast<int32_t>(as_kind(kind_at).frame_count);
        for (int32_t f = frames < 0x20 ? 0 : frames; f < frames; ++f) {
            guest_array<uint8_t>(texture_used)[as_kind(kind_at).meshes[static_cast<uint32_t>(f)]] =
                1;
        }
        guest_array<uint32_t>(first_texture)[k] = as_kind(kind_at).meshes[0];
    }
    for (uint32_t t = 0; t < MESH_LIMIT; ++t) {
        if (guest_array<uint8_t>(texture_used)[t] != 1) {
            continue;
        }
        const uint32_t image = MESHES + t * MESH_SIZE;
        image_apply(as_image(image), 1, as_pack(course_pack()), course_record().mesh_textures + t);
        as_mesh(image).texture_index = as_mesh(image).texture_index + IMAGE_TEXTURES_BEFORE;
    }

    for (uint32_t o = 0; o < count; ++o) {
        const uint32_t record = table + o * OBJECT_SIZE;
        const uint32_t k = as_object(record).kind;
        const uint32_t kind_at = OBJECT_KINDS + k * OBJECT_KIND_SIZE;
        if (k >= KIND_LIMIT || as_kind(kind_at).meshes[0] >= MESH_LIMIT) {
            assert_trap(0x18011bfcu);
        }
        const int32_t angle = static_cast<int32_t>(as_object(record).angle);
        const int32_t sin = sine_degrees(-to_fixed_signed(angle));
        const int32_t cos = sine_degrees(to_fixed_signed(90) - to_fixed_signed(angle));
        const uint32_t matrix = record + static_cast<uint32_t>(offsetof(HoleObject, matrix));
        const auto set = [matrix](uint32_t index, uint32_t value) {
            guest<uint32_t>(matrix + index * 4) = value;
        };
        libc::memory_clear(matrix, MATRIX_WORDS * 4);
        set(4, static_cast<uint32_t>(sin));
        set(1, static_cast<uint32_t>(-sin));
        set(0, static_cast<uint32_t>(cos));
        set(10, 0x10000);
        set(5, static_cast<uint32_t>(cos));
        set(15, 0x10000);
        const int32_t frames = static_cast<int32_t>(as_kind(kind_at).frame_count);
        for (int32_t f = 0; f < frames; ++f) {
            const uint32_t image =
                MESHES + as_kind(kind_at).meshes[static_cast<uint32_t>(f)] * MESH_SIZE;
            const int32_t left = static_cast<int32_t>(as_mesh(image).left),
                          top = static_cast<int32_t>(as_mesh(image).top);
            const int32_t width = static_cast<int32_t>(as_mesh(image).width);
            const int32_t right = top - static_cast<int32_t>(as_mesh(image).u) - width;
            const int32_t below = static_cast<int32_t>(as_mesh(image).height) -
                                  static_cast<int32_t>(as_mesh(image).v) - left;
            const uint32_t quad = record + static_cast<uint32_t>(offsetof(HoleObject, quads)) +
                                  static_cast<uint32_t>(f) * 4 * 4;
            const uint32_t quad_b = record + static_cast<uint32_t>(offsetof(HoleObject, quads_b)) +
                                    static_cast<uint32_t>(f) * 4 * 4;
            const auto shift = [](uint32_t at, int32_t by) {
                uint32_t& corner = guest<uint32_t>(at);
                corner = static_cast<uint32_t>(static_cast<int32_t>(corner) + to_fixed_signed(by));
            };
            shift(quad + 0 * 4, width);
            shift(quad_b + 0 * 4, -below);
            shift(quad + 1 * 4, -right);
            shift(quad_b + 1 * 4, -below);
            shift(quad + 2 * 4, -right);
            shift(quad_b + 2 * 4, left);
            shift(quad + 3 * 4, width);
            shift(quad_b + 3 * 4, left);
        }
        const int32_t rate = static_cast<int32_t>(as_kind(kind_at).rate);
        if (rate == 0) {
            as_object(record).timer = 0;
        } else {
            as_object(record).timer = 1;
            as_object(record).timer =
                static_cast<uint32_t>((static_cast<int64_t>(FRAMES_PER_SECOND) << 32) /
                                      (static_cast<int64_t>(rate) << 16));
        }
        as_object(record).elapsed = 0;
        as_object(record).frame =
            static_cast<uint8_t>(static_cast<uint32_t>(as_object(record).first_frame));
        as_object(record).direction = static_cast<int8_t>(1);
        switch (as_kind(kind_at).trigger) {
        case 0:
            as_object(record).triggered = static_cast<uint8_t>(1);
            break;
        case 2:
            as_object(record).triggered = static_cast<uint8_t>(0);
            as_object(record).wait =
                random_next(game_state_block().object_24, as_kind(kind_at).wait);
            break;
        case 1:
            as_object(record).triggered = static_cast<uint8_t>(0);
            as_object(record).wait = as_kind(kind_at).wait;
            break;
        default:
            break;
        }
    }
}

// 0x180043f4 — the images a hole is drawn with, from the course pack: the fixed ones (badges,
// sprites, markers; the strokes badge differs for pass 'n play), the five course pictures the
// hole uses, and the four ball images. Each image's texture index is moved past the textures
// loaded before it.
void hole_images_load() {
    const uint32_t pack = game_state_block().pack_handle;
    const auto load = [&](uint32_t at, uint32_t id, uint32_t textures_before) {
        ImageRecord& image = as_image(at);
        image_apply(image, 1, as_pack(pack), id);
        image.texture_index = image.texture_index + textures_before;
    };
    load(GAME_STATE + 0x84e48, 0x1a, IMAGE_TEXTURES_FIXED);
    load(GAME_STATE + 0x84e0c, static_cast<uint32_t>(menu_state().game_mode) == 1 ? 0x1c : 0x1b,
         IMAGE_TEXTURES_FIXED);
    load(GAME_STATE + 0x84e84, 0x14, IMAGE_TEXTURES_FIXED);
    load(GAME_STATE + 0x84ec0, 0x15, IMAGE_TEXTURES_FIXED);
    image_apply(as_image(GAME_STATE + 0x85154), 1, as_pack(pack), 0x16);
    as_image(GAME_STATE + 0x85154).texture_index = 5;
    image_apply(as_image(GAME_STATE + 0x85190), 1, as_pack(pack), 0x17);
    as_image(GAME_STATE + 0x85190).texture_index = 5;
    load(GAME_STATE + 0x84efc, 0x12, IMAGE_TEXTURES_FIXED);
    load(GAME_STATE + 0x84f38, 0x13, IMAGE_TEXTURES_FIXED);
    load(GAME_STATE + 0x84f74, 0x11, IMAGE_TEXTURES_FIXED);
    load(GAME_STATE + 0x85118, 0x19, IMAGE_TEXTURES_FIXED);
    load(GAME_STATE + 0x850dc, 0x18, IMAGE_TEXTURES_FIXED);
    for (uint32_t i = 0; i < 5; ++i) {
        image_apply(as_image(GAME_STATE + 0x84fb0 + i * image::SIZE), 1, as_pack(course_pack()),
                    (course_record().pictures + i));
        ImageRecord& picture = as_image(GAME_STATE + 0x84fb0 + i * image::SIZE);
        picture.texture_index = picture.texture_index + IMAGE_TEXTURES_BEFORE;
    }
    for (uint32_t i = 0; i < 4; ++i) {
        image_apply(as_image(GAME_STATE + 0x84d1c + i * image::SIZE), 1, as_pack(course_pack()),
                    (course_record().balls + i));
        ImageRecord& ball = as_image(GAME_STATE + 0x84d1c + i * image::SIZE);
        ball.texture_index = ball.texture_index + IMAGE_TEXTURES_FIXED;
    }
    as_image(GAME_STATE + 0x84dd0).texture_index = 5;
}

// 0x18009908 — the ball on the tee for the hole's start: both players' rest records take the
// tee, the ball is still, and the state is placing — or the stroke limit's wait if the current
// player has already used their ten.
void hole_ball_place() {
    ball_to_tee();
    for (const uint32_t player : {0u, 1u}) {
        PlayerRecord& record = player_record(player);
        record.ball_rest_x = play_state().ball_x;
        record.ball_rest_y = play_state().ball_y;
        record.ball_rest_angle = 0x40;
    }
    play_state().velocity_x = 0;
    play_state().velocity_y = 0;
    play_state().state = 1;
    play_state().word_644 = 0;
    play_state().state_frames = 0;
    play_state().step_remainder = 0;
    const uint32_t current = static_cast<uint32_t>(static_cast<int16_t>(players_state().current));
    if (static_cast<int16_t>(player_record(current).strokes) >= 10) {
        play_state().state = 0xf;
    }
}

// 0x18009788 — set the hole up once its layout is in memory: the tile map, the frame tables
// and sprite sheets from the layout's sections, the objects, the images, the power meter's
// constants, the ball on the tee, and both players' strokes back to none.
void hole_setup() {
    guest<uint32_t>(TILE_MAP) = TILE_MAP_STORE;
    tile_map_build();
    const uint32_t sections = guest<uint32_t>(COURSE_DATA) + LAYOUT_SECTIONS;
    uint32_t offset = 8;
    const int32_t skipped = static_cast<int32_t>(as_section(sections).count);
    for (int32_t i = 0; i < skipped; ++i) {
        offset += as_section(sections + offset).length;
    }
    const int32_t frame_tables = static_cast<int16_t>(
        guest<uint8_t>(sections + offset) | (guest<uint8_t>(sections + offset + 1) << 8) |
        (guest<uint8_t>(sections + offset + 2) << 16) |
        (guest<uint8_t>(sections + offset + 3) << 24));
    offset += 4;
    const uint32_t table = tracked_allocate(static_cast<uint32_t>(frame_tables) * 4 + 1);
    if (table == 0) {
        assert_trap(0x18009834u);
    }
    for (int32_t i = 0; i < frame_tables; ++i) {
        guest<uint32_t>(table + static_cast<uint32_t>(i) * 4) = sections + offset;
        offset += 2 + static_cast<uint32_t>(as_frames(sections + offset).count) * 2;
    }
    players_state().kind_limit_a = static_cast<uint16_t>(static_cast<uint32_t>(frame_tables));
    play_state().frame_counts_a = table;
    sprite_sheets_load(as_section(sections),
                       PLAYERS + static_cast<uint32_t>(offsetof(PlayersState, sprite_sheet_count)),
                       PLAY + static_cast<uint32_t>(offsetof(PlayState, sprite_sheets)), 0);
    hole_objects_prepare();
    hole_images_load();
    play_state().meter_scale = 0xc000;
    play_state().meter_max = 0x30000;
    play_state().meter_step = 0x1000;
    play_state().meter_start = 1;
    play_state().meter_speed = 2;
    play_state().word_61c = 4;
    play_state().word_620 = 7;
    play_state().word_624 = 0;
    hole_ball_place();
    for (const uint32_t player : {0u, 1u}) {
        player_record(player).strokes = static_cast<uint16_t>(0);
        player_record(player).result = static_cast<uint16_t>(0);
    }
    players_state().current = static_cast<int16_t>(0);
}

// 0x180154bc — the course and hole data from the course pack: the course's file (kept whole;
// the hole setup reads its sections), then the hole's — its physics constants, its tables of
// tee points, edges, animated objects, obstacles and surfaces — from which the walls (edges
// with the rough on one side and not the other) and the pegs at their ends are built, each
// with a unit normal.
// Read the course's data resource and this hole's, into the tracked heap and HOLE_DATA.
void course_resources_load() {
    const int32_t course = menu_state().course, hole = menu_state().hole;
    if (course_pack() == 0 || course < 0 || course >= 3 || hole < 0 || hole >= 0x12) {
        assert_trap(0x180154dcu);
    }
    const uint32_t course_id = course_record().id;
    const uint32_t data_size = resource_size(as_pack(course_pack()), course_id);
    guest<uint32_t>(COURSE_DATA_SIZE) = data_size;
    const uint32_t data = tracked_allocate(data_size);
    guest<uint32_t>(COURSE_DATA) = data;
    if (data == 0) {
        assert_trap(0x18015544u);
    }
    resource_load(as_pack(course_pack()), course_id, data, data_size);
    resource_load(as_pack(course_pack()), course_id + static_cast<uint32_t>(hole) + 1, HOLE_DATA,
                  HOLE_DATA_SIZE);
}

// The physics constants at the head of the hole data: the meter's gain, friction, restitution,
// the two slope pulls and one more, then sixteen words each split into a low and a high half.
void hole_constants_read() {
    // one more), then sixteen words split into halves.
    PlayState& play = play_state();
    const uint32_t* header = guest_array<uint32_t>(HOLE_DATA);  // the hole data's words
    play.meter_gain = header[1];
    play.friction = header[2];
    play.restitution = header[3];
    play.slope_gentle = header[4];
    play.slope_steep = header[5];
    play.hole_constants[0] = header[6];
    for (uint32_t i = 0; i < 2; ++i) {
        const uint32_t word = header[7 + i];
        play.hole_constants[1 + i * 2] = word & 0xffff;
        play.hole_constants[2 + i * 2] = static_cast<uint32_t>(to_whole(word));
    }
    play.hole_constants[5] = header[9];
    for (uint32_t i = 0; i < 16; ++i) {
        const uint32_t word = header[10 + i];
        players_state().hole_words_low[i] = static_cast<uint16_t>(word);
        players_state().hole_words_high[i] =
            static_cast<uint16_t>(static_cast<uint32_t>(to_whole(word)));
    }
    const uint32_t split_word = play.hole_constants[0];
    play.hole_constants[0] = split_word & 0xffff;
    play.hole_constants[22] = static_cast<uint32_t>(to_whole(split_word));
    play_state().animation_phase = 0;
}

// The hole's tables, which follow the header as sized sections — each a length word and then its
// entries: the tee points and edges, the animated sprites, the obstacles and the surfaces.
void hole_tables_read() {
    const uint32_t* header = guest_array<uint32_t>(HOLE_DATA);
    uint32_t section = HOLE_DATA + header[0];
    const auto entries = [&](uint32_t entry_size) {
        return (static_cast<int32_t>(as_section(section).length) - 4) /
               static_cast<int32_t>(entry_size);
    };
    const auto next = [&]() { section += as_section(section).length; };
    players_state().tee_point_count = static_cast<uint16_t>(static_cast<uint32_t>(entries(4)));
    play_state().tee_points = section + 4;
    next();
    const libc::Division edges = libc::signed_divide(as_section(section).length - 4, 12);
    players_state().tee_edge_count = static_cast<uint16_t>(edges.quotient);
    play_state().tee_edges = section + 4;
    next();
    const libc::Division section_2 = libc::signed_divide(as_section(section).length - 4, 12);
    players_state().section_2_count = static_cast<uint16_t>(section_2.quotient);
    play_state().word_5d8 = section + 4;
    next();
    const int32_t objects = static_cast<int16_t>(entries(8));
    players_state().object_count =
        static_cast<uint16_t>(static_cast<int16_t>(static_cast<uint32_t>(objects)));
    const uint32_t sprites = tracked_allocate(static_cast<uint32_t>(objects) * 12);
    play_state().objects = sprites;
    if (sprites != 0) {
        for (int32_t i = 0; i < objects; ++i) {
            const HoleSpriteEntry& from =
                table_entry<HoleSpriteEntry>(section + 4, static_cast<uint32_t>(i));
            HoleSprite& to = table_entry<HoleSprite>(sprites, static_cast<uint32_t>(i));
            to.x = from.x;
            to.y = from.y;
            to.word_4 = from.word_6;
            to.frame = 0;
            to.kind = from.kind;
        }
    }
    next();
    players_state().obstacle_count = static_cast<uint16_t>(static_cast<uint32_t>(entries(8)));
    play_state().obstacle_table = section + 4;
    next();
    play_state().surface_table = section + 4;
    play_state().word_5e8 = section + as_section(section).length - HOLE_DATA;
}

// The walls: an edge with the rough (kind 2) on one side and a solid kind on the other, pointing
// so the rough is on its left. Each gets a unit normal for the ball to bounce off.
void walls_build() {
    const auto solid = [](uint32_t k) { return k != 2 && k != 1 && k != 6; };
    const int32_t edge_count = static_cast<int16_t>(players_state().tee_edge_count);
    const auto edge_at = [](int32_t i) -> const TeeEdge& {
        return tee_edge_at(static_cast<uint32_t>(i));
    };
    int32_t walls = 0;
    for (int32_t i = 0; i < edge_count; ++i) {
        const uint32_t a = edge_at(i).kind_down, b = edge_at(i).kind_up;
        if ((b == 2 && solid(a)) || (a == 2 && solid(b))) {
            ++walls;
        }
    }
    players_state().wall_count =
        static_cast<uint16_t>(static_cast<uint32_t>(static_cast<int16_t>(walls)));
    play_state().wall_table =
        tracked_allocate(static_cast<uint32_t>(static_cast<int16_t>(walls)) * 16);
    const auto point = [](int32_t index) -> const TeePoint& {
        return tee_point_at(static_cast<uint32_t>(index));
    };
    if (play_state().wall_table != 0) {
        uint32_t wall = 0;
        for (int32_t i = 0; i < edge_count; ++i) {
            const TeeEdge& edge = edge_at(i);
            const uint32_t a = edge.kind_down, b = edge.kind_up;
            const TeePoint &pa = point(edge.a), &pb = point(edge.b);
            if (b == 2 && solid(a)) {
                Wall& w = wall_at(wall++);
                w.x0 = pb.x;
                w.y0 = pb.y;
                w.x1 = pa.x;
                w.y1 = pa.y;
            }
            if (a == 2 && solid(b)) {
                Wall& w = wall_at(wall++);
                w.x0 = pa.x;
                w.y0 = pa.y;
                w.x1 = pb.x;
                w.y1 = pb.y;
            }
        }
    }
    for (int32_t i = 0; i < static_cast<int16_t>(players_state().wall_count); ++i) {
        Wall& wall = wall_at(static_cast<uint32_t>(i));
        const int32_t dy = wall.y1 - wall.y0, dx = wall.x0 - wall.x1;
        const uint32_t length = integer_sqrt(static_cast<uint32_t>((dy * dy + dx * dx) << 16));
        const libc::Division unit_x =
            libc::signed_divide(static_cast<uint32_t>(to_fixed(dy)), length);
        wall.nx = static_cast<int32_t>(unit_x.quotient);
        const libc::Division unit_y =
            libc::signed_divide(static_cast<uint32_t>(to_fixed(dx)), length);
        wall.ny = static_cast<int32_t>(unit_y.quotient);
    }
}

// The pegs: the points the walls end at, each with the sum of its walls' normals, so a ball that
// reaches a corner is pushed out along the average of the two faces meeting there.
void pegs_build() {
    const auto solid = [](uint32_t k) { return k != 2 && k != 1 && k != 6; };
    const int32_t edge_count = static_cast<int16_t>(players_state().tee_edge_count);
    const auto edge_at = [](int32_t i) -> const TeeEdge& {
        return tee_edge_at(static_cast<uint32_t>(i));
    };
    const auto point = [](int32_t index) -> const TeePoint& {
        return tee_point_at(static_cast<uint32_t>(index));
    };
    uint16_t* marks = guest_array<uint16_t>(as_pack(game_state_block().pack_handle).scratch);
    const int32_t points = static_cast<int16_t>(players_state().tee_point_count);
    for (int32_t i = 0; i < points; ++i) {
        marks[i] = 0;
    }
    for (int32_t i = 0; i < edge_count; ++i) {
        const TeeEdge& edge = edge_at(i);
        const uint32_t a = edge.kind_down, b = edge.kind_up;
        if ((b == 2 && solid(a)) || (a == 2 && solid(b))) {
            marks[edge.a] = 1;
            marks[edge.b] = 1;
        }
    }
    int32_t pegs = 0;
    for (int32_t i = 0; i < points; ++i) {
        if (marks[i] == 1) {
            ++pegs;
        }
    }
    players_state().peg_count =
        static_cast<uint16_t>(static_cast<uint32_t>(static_cast<int16_t>(pegs)));
    play_state().peg_table =
        tracked_allocate(static_cast<uint32_t>(static_cast<int16_t>(pegs)) * 12);
    if (play_state().peg_table != 0) {
        uint32_t peg = 0;
        for (int32_t i = 0; i < points; ++i) {
            if (marks[i] != 1) {
                continue;
            }
            Peg& p = peg_at(peg++);
            p.x = point(i).x;
            p.y = point(i).y;
            p.nx = 0;
            p.ny = 0;
        }
    }
    for (int32_t p = 0; p < static_cast<int16_t>(players_state().peg_count); ++p) {
        Peg& peg = peg_at(static_cast<uint32_t>(p));
        for (int32_t w = 0; w < static_cast<int16_t>(players_state().wall_count); ++w) {
            const Wall& wall = wall_at(static_cast<uint32_t>(w));
            const bool at_start = peg.x == wall.x0 && peg.y == wall.y0;
            const bool at_end = peg.x == wall.x1 && peg.y == wall.y1;
            if (at_start || at_end) {
                peg.nx = static_cast<int32_t>(static_cast<uint32_t>(peg.nx) +
                                              static_cast<uint32_t>(wall.nx));
                peg.ny = static_cast<int32_t>(static_cast<uint32_t>(peg.ny) +
                                              static_cast<uint32_t>(wall.ny));
            }
        }
    }
    for (int32_t p = 0; p < static_cast<int16_t>(players_state().peg_count); ++p) {
        Peg& peg = peg_at(static_cast<uint32_t>(p));
        const int32_t nx = peg.nx, ny = peg.ny;
        const uint32_t length = integer_sqrt(static_cast<uint32_t>(nx * nx + ny * ny));
        if (length == 0) {
            peg.nx = 0;
            peg.ny = 0;
            continue;
        }
        const libc::Division peg_unit_x =
            libc::signed_divide(static_cast<uint32_t>(nx << 8), length);
        peg.nx = static_cast<int32_t>(peg_unit_x.quotient);
        const libc::Division peg_unit_y =
            libc::signed_divide(static_cast<uint32_t>(ny << 8), length);
        peg.ny = static_cast<int32_t>(peg_unit_y.quotient);
    }
}

// 0x18015490 — everything a hole needs from its resources, in the order the file lays it out.
void course_data_load() {
    course_resources_load();
    hole_constants_read();
    hole_tables_read();
    walls_build();
    pegs_build();
}

// 0x1800e868 — the hole's object layout from the course pack, the wipe prepared, and the
// ground texture assembled from the course's 80×60 tiles (a white tile stands in for one the
// course lacks) into a 720×600 texture.
void hole_layout_load() {
    GuestScratch frame(4 * 9 + 0x14);
    const uint32_t hole = static_cast<uint32_t>(menu_state().hole);
    resource_load(as_pack(course_pack()), course_record().layouts + hole + 1, HOLE_LAYOUT,
                  HOLE_LAYOUT_SIZE);
    hole_objects_state().count = guest<uint32_t>(HOLE_LAYOUT);
    hole_objects_state().cursor = HOLE_LAYOUT + 4;
    hole_objects_state().source = HOLE_LAYOUT + 4 + LAYOUT_OBJECTS;
    if (static_cast<uint8_t>(as_source(HOLE_LAYOUT + 4).kind) != 1) {
        assert_trap(0x1800e8e0u);
    }
    ground_camera_prepare(0);
    const uint32_t heap_mark = game_state_block().word_28;
    if (static_cast<int32_t>(heap_mark) < 0x82b80) {
        assert_trap(0x1800e8fcu);
    }
    game_state_block().word_28 = 0x19000;
    course_loader().ground_store = GROUND_TILES_STORE;
    const uint32_t first_column = static_cast<uint32_t>(hole_objects_state().grid[1]),
                   first_row = static_cast<uint32_t>(hole_objects_state().grid[2]);
    const uint32_t columns = static_cast<uint32_t>(hole_objects_state().grid[3]),
                   rows = static_cast<uint32_t>(hole_objects_state().grid[4]);
    const uint32_t tiles = course_record().ground_tiles;
    hole_objects_state().origin_x = first_column * (to_fixed(GROUND_TILE_WIDTH));
    hole_objects_state().origin_y = first_row * (to_fixed(GROUND_TILE_HEIGHT));
    bool palette_kept = false;
    for (uint32_t row = 0; row < rows; ++row) {
        for (uint32_t column = 0; column < columns; ++column) {
            const uint32_t id =
                tiles + ((rows - row + first_row) << 4) + first_column + column - 0x10;
            uint32_t pixels;
            if (tiles + GROUND_TILE_COUNT > id) {
                pixels = resource_open(as_pack((course_pack())), id, 0, 0);
                if (!palette_kept) {
                    libc::memory_copy(course_loader().ground_store, pixels, GROUND_PALETTE_BYTES);
                    palette_kept = true;
                }
                pixels += GROUND_PALETTE_BYTES;
            } else {
                libc::memory_fill(GROUND_TILE_SCRATCH, GROUND_TILE_WIDTH * GROUND_TILE_HEIGHT,
                                  0xff);
                pixels = GROUND_TILE_SCRATCH;
            }
            ground_tile_blit(pixels, column * GROUND_TILE_WIDTH, row * GROUND_TILE_HEIGHT);
        }
    }
    texture_from_pixels(as_image(GROUND_IMAGE), 0, GROUND_TEXTURE_WIDTH, GROUND_TEXTURE_HEIGHT,
                        course_loader().ground_store, TEXTURE_GROUND);
    game_state_block().word_28 = heap_mark;
    course_loader().ground_store = 0;
    play_state().byte_630 = static_cast<uint8_t>(0);
}

// 0x1800f9b0 — a vector (x, y, z, w in 16.16) through a 4×4 matrix (row-major, 16.16); the
// result's w is always one.
// A matrix read out of guest memory into an ordinary array. The matrices live in packed
// overlays, whose arrays cannot portably be passed as pointers — a compiler is entitled to
// assume a `uint32_t*` is aligned, and in a packed structure it need not be.
void matrix_read(uint32_t at, uint32_t (&out)[MATRIX_WORDS]) {
    for (uint32_t i = 0; i < MATRIX_WORDS; ++i) {
        out[i] = guest<uint32_t>(at + i * 4);
    }
}

void matrix_transform(const uint32_t* matrix, const uint32_t* vector, uint32_t* out) {
    for (uint32_t j = 0; j < 3; ++j) {
        int64_t sum = 0;
        for (uint32_t k = 0; k < 4; ++k) {
            const int64_t product = static_cast<int64_t>(static_cast<int32_t>(vector[k])) *
                                    static_cast<int32_t>(matrix[k * 4 + j]);
            sum += product >> 16;
        }
        out[j] = static_cast<uint32_t>(static_cast<int32_t>(sum));
    }
    out[3] = 0x10000;
}

// 0x1800faf8 — the ground's camera from the layout's first entry: where the ground sits,
// its angle (as two rotation matrices, the same one twice), the ground tile grid it shows,
// and the four corners of the ground quad turned by that angle. `first` keeps the corner
// store from an earlier hole; otherwise it is cleared.
void ground_camera_prepare(uint32_t first) {
    if (hole_objects_state().cursor == 0) {
        assert_trap(0x1800fb0cu);
    }
    ObjectSource& entry = as_source(hole_objects_state().cursor);
    if (first == 0) {
        libc::memory_clear(HOLE_OBJECTS + offsetof(HoleObjects, corners_x), 0xf8);
    }
    if (static_cast<int32_t>(entry.y) > static_cast<int32_t>(GROUND_Y_LIMIT)) {
        entry.y = GROUND_Y_LIMIT;
    }
    HoleObjects& ground = hole_objects_state();
    ground.base_x = entry.x;
    ground.base_y = 0u - entry.y;
    const int32_t angle = entry.angle;
    ground.angle = static_cast<uint32_t>(angle);
    // The kind word and the four after it, copied between two packed overlays by address.
    libc::memory_copy(HOLE_OBJECTS + static_cast<uint32_t>(offsetof(HoleObjects, grid)),
                      address_of(entry) + static_cast<uint32_t>(offsetof(ObjectSource, kind)),
                      static_cast<uint32_t>(sizeof ground.grid));
    const int32_t sin = sine_degrees(to_fixed_signed(angle));
    const int32_t cos = sine_degrees(to_fixed_signed(90) + to_fixed_signed(angle));
    for (const size_t field : {offsetof(HoleObjects, matrix_a), offsetof(HoleObjects, matrix_b)}) {
        const uint32_t matrix = HOLE_OBJECTS + static_cast<uint32_t>(field);
        const auto set = [matrix](uint32_t index, uint32_t value) {
            guest<uint32_t>(matrix + index * 4) = value;
        };
        libc::memory_clear(matrix, MATRIX_WORDS * 4);
        set(0, static_cast<uint32_t>(cos));
        set(1, static_cast<uint32_t>(sin));
        set(4, static_cast<uint32_t>(-sin));
        set(5, static_cast<uint32_t>(cos));
        set(10, 0x10000);
        set(15, 0x10000);
    }
    uint32_t matrix_a[MATRIX_WORDS];
    matrix_read(HOLE_OBJECTS + static_cast<uint32_t>(offsetof(HoleObjects, matrix_a)), matrix_a);
    for (uint32_t i = 0; i < 4; ++i) {
        const uint32_t vector[4] = {entry.quad[i], entry.quad_b[i], 0, 0x10000};
        uint32_t result[4];
        matrix_transform(matrix_a, vector, result);
        ground.corners_x[i] = result[0];
        ground.corners_y[i] = result[1];
    }
}

// 0x180042f4 — the images the course select and the hole share once a course is chosen: the
// hole's picture and the four ball images from the course pack.
void course_images_load() {
    if (game_state_block().pack_handle == 0 || course_pack() == 0) {
        assert_trap(0x18004308u);
    }
    const int32_t unlocked =
        static_cast<int32_t>(static_cast<uint32_t>(static_cast<int8_t>(save_data_byte(0x19))));
    if (unlocked <= 0 || unlocked > 3) {
        assert_trap(0x18004338u);
    }
    image_apply(as_image(GAME_STATE + 0x84ec0), 0, as_pack(game_state_block().pack_handle), 0xa);
    as_image(GAME_STATE + 0x84ec0).texture_index =
        as_image(GAME_STATE + 0x84ec0).texture_index + IMAGE_TEXTURES_FIXED;
    for (uint32_t i = 0; i < 4; ++i) {
        ImageRecord& ball = as_image(GAME_STATE + 0x84d1c + i * image::SIZE);
        image_apply(ball, 1, as_pack(course_pack()), course_record().balls + i);
        ball.texture_index = ball.texture_index + IMAGE_TEXTURES_FIXED;
    }
    as_image(GAME_STATE + 0x84d40).texture_index = 0x18;
    as_image(GAME_STATE + 0x84dd0).texture_index = 5;
}

}  // namespace minigolf::game
