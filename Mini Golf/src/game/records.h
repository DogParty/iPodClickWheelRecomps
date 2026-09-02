// The records the game reads out of its course and hole files, and the tables the hole loader
// builds from them (hole_load.cpp). Like state.h these are views over guest memory: the
// file data is loaded there and the tables are allocated there, so the original's layouts hold.
#pragma once

#include "state.h"

namespace minigolf::game {

// The hole data (HOLE_DATA): sized sections one after another, each a length word, a count
// word for some, then its entries.
struct [[gnu::packed]] Section {
    uint32_t length;  // including this word
    uint32_t count;
};

struct [[gnu::packed]] TeePoint {
    int16_t x, y;
};
static_assert(sizeof(TeePoint) == 4);

// An edge of the tee/green outline: two point indices, Bézier control offsets (all zero for
// a straight edge), the tile kinds on each side, and the number of pieces a curve is drawn in.
struct [[gnu::packed]] TeeEdge {
    int16_t a, b;
    int8_t control[4];  // c0x, c0y, c1x, c1y
    uint8_t kind_down, kind_up;
    uint8_t byte_a;
    uint8_t steps;
};
static_assert(sizeof(TeeEdge) == 12);

// An animated object in the hole data (8 bytes) and the 12-byte entry the loader builds.
struct [[gnu::packed]] HoleSpriteEntry {
    int16_t x, y, kind, word_6;
};
static_assert(sizeof(HoleSpriteEntry) == 8);
struct [[gnu::packed]] HoleSprite {
    int16_t x, y;
    int16_t word_4;  // the entry's +6
    int16_t frame;
    int16_t frame_saved;  // kept across a fast forward
    int16_t kind;         // >= 0x800 (0x1000 in the renderer's test) for the second kind table
};
static_assert(sizeof(HoleSprite) == 12);

// A kind's animation: the frame count then a frame word each (sheet in the low 10 bits).
struct [[gnu::packed]] FrameList {
    int16_t count;
    uint16_t frames[0x3ff];
};

struct [[gnu::packed]] Obstacle {
    int16_t x, y;
    uint32_t kind;
};
static_assert(sizeof(Obstacle) == 8);

// A special surface (tiles 0x30..0x3f): where the ball goes and how its velocity changes.
struct [[gnu::packed]] Surface {
    uint16_t dx, dy;
    int32_t ax, ay;
    uint32_t flags;
};
static_assert(sizeof(Surface) == 16);

// A wall the ball bounces off: its ends and its unit normal (16.16), rough on the left.
struct [[gnu::packed]] Wall {
    int16_t x0, y0, x1, y1;
    int32_t nx, ny;
};
static_assert(sizeof(Wall) == 16);

// A peg: a point where walls meet, with the sum of their normals (8.8).
struct [[gnu::packed]] Peg {
    int16_t x, y;
    int32_t nx, ny;
};
static_assert(sizeof(Peg) == 12);

// One sprite sheet of the animated objects (the loader's 0x48-byte table entry).
struct [[gnu::packed]] SpriteSheet {
    uint16_t width, height;
    uint32_t word_4;  // zero marks a sheet nothing refers to
    uint32_t data;    // -> the pixels in the layout section
    ImageRecord image;
    uint8_t pad_45[0x3];
};
static_assert(offsetof(SpriteSheet, image) == 0xc);
static_assert(sizeof(SpriteSheet) == 0x48);

// A 3-D object's entry in the hole layout (0x30 bytes); the first is the ground's camera.
struct [[gnu::packed]] ObjectSource {
    uint32_t quad[4];
    uint32_t quad_b[4];
    uint32_t x, y;
    int16_t angle;
    int16_t kind;
    uint8_t byte_2c;
    int8_t behind_ball;
    int8_t first_frame;
    uint8_t pad_2f;
};
static_assert(sizeof(ObjectSource) == 0x30);

// A mesh the ball has to get past (MESH_VISITS, 23 entries): which course/hole/meshes it
// belongs to, its rectangle, and whether it is solid.
struct [[gnu::packed]] MeshVisit {
    uint32_t course, hole, mesh_a, mesh_b;
    int32_t x, y, width, height;
    uint8_t solid;
    uint8_t pad_21[0x3];
};
static_assert(sizeof(MeshVisit) == 36);

// The i-th record of a table at a guest address.
template <class T>
T& table_entry(uint32_t base, uint32_t index) {
    return guest<T>(base + index * static_cast<uint32_t>(sizeof(T)));
}
inline Section& as_section(uint32_t a) {
    return guest<Section>(a);
}
inline TeePoint& tee_point_at(uint32_t i) {
    return table_entry<TeePoint>(play_state().tee_points, i);
}
inline TeeEdge& tee_edge_at(uint32_t i) {
    return table_entry<TeeEdge>(play_state().tee_edges, i);
}
inline HoleSprite& sprite_at(uint32_t i) {
    return table_entry<HoleSprite>(play_state().objects, i);
}
inline Obstacle& obstacle_at(uint32_t i) {
    return table_entry<Obstacle>(play_state().obstacle_table, i);
}
inline Surface& surface_at(uint32_t i) {
    return table_entry<Surface>(play_state().surface_table, i);
}
inline Wall& wall_at(uint32_t i) {
    return table_entry<Wall>(play_state().wall_table, i);
}
inline Peg& peg_at(uint32_t i) {
    return table_entry<Peg>(play_state().peg_table, i);
}
inline FrameList& as_frames(uint32_t a) {
    return guest<FrameList>(a);
}
inline SpriteSheet& as_sheet(uint32_t a) {
    return guest<SpriteSheet>(a);
}
inline ObjectSource& as_source(uint32_t a) {
    return guest<ObjectSource>(a);
}
inline MeshVisit& mesh_visit(uint32_t i) {
    return table_entry<MeshVisit>(MESH_VISITS, i);
}
inline uint8_t& mesh_seen(uint32_t i) {
    return guest<uint8_t>(MESH_SEEN + i);
}
inline PlayerRecord& as_player(uint32_t a) {
    return guest<PlayerRecord>(a);
}
inline SpriteSheet& sheet_at(uint32_t table, uint32_t i) {
    return table_entry<SpriteSheet>(table, i);
}
// A sprite sheet as it sits in the layout section: its length, size, and pixels from +7.
struct [[gnu::packed]] SheetSource {
    uint32_t length;
    uint8_t width, height, byte_6;
};
inline SheetSource& as_sheet_source(uint32_t a) {
    return guest<SheetSource>(a);
}
// The kind's frame list of a sprite: the second kind table for kinds at or above `high`.
inline FrameList& sprite_frames(int32_t kind, int32_t high) {
    return as_frames(
        kind >= high ? ld32(play_state().frame_counts_b + static_cast<uint32_t>(kind - high) * 4)
                     : ld32(play_state().frame_counts_a + static_cast<uint32_t>(kind) * 4));
}

// The course information table (COURSE_INFO_TABLE, one per course): the resource ids of
// what the course's pack holds, where the hole's badges and text sit, the panel tint, and
// the par of every hole.
struct [[gnu::packed]] CourseInfo {
    uint32_t id;  // the course data resource
    uint32_t layouts;
    uint32_t first_hole_text;
    uint32_t pictures;
    uint32_t ground_tiles;
    uint32_t sheets;
    uint32_t mesh_textures;
    uint32_t balls;
    uint32_t hole_badge_x, hole_badge_y, hole_text_x, hole_text_y;
    uint32_t strokes_x, strokes_y;
    uint32_t result_badge_y;
    uint8_t pad_3c[0xc];
    uint32_t tint_r, tint_g, tint_b;
    int8_t pars[20];
};
static_assert(offsetof(CourseInfo, hole_badge_x) == 0x20);
static_assert(offsetof(CourseInfo, tint_r) == 0x48);
static_assert(offsetof(CourseInfo, pars) == 0x54);
static_assert(sizeof(CourseInfo) == 13 * 8);
constexpr uint32_t COURSE_INFO_TABLE = 0x1801'9010;
inline CourseInfo& course_info_at(int32_t course) {
    return table_entry<CourseInfo>(COURSE_INFO_TABLE, static_cast<uint32_t>(course));
}

// The file-kind table (0x18019148): 12 rows (one per language text) of 16 entries naming the
// score files, their sizes, and whether the course owns them.
struct [[gnu::packed]] FileKind {
    uint32_t name;  // -> the file name
    uint32_t size;
    uint8_t owned_by_course;
    uint8_t pad_9[0x3];
};
static_assert(sizeof(FileKind) == 0xc);
constexpr uint32_t FILE_KINDS = 0x1801'9148, FILE_KIND_ROW = 0xc0;
inline FileKind& file_kind(uint32_t text, uint32_t entry) {
    return table_entry<FileKind>(FILE_KINDS + text * FILE_KIND_ROW, entry);
}
inline FileKind& as_file_kind(uint32_t address) {
    return guest<FileKind>(address);
}

// A file record of the save/score loaders (FILE_RECORDS, 0x204 bytes): its file object and name.
struct [[gnu::packed]] FileRecord {
    uint32_t object;
    uint8_t name[0x200];
};
static_assert(sizeof(FileRecord) == 0x204);
inline FileRecord& as_file_record(uint32_t address) {
    return guest<FileRecord>(address);
}

// The live save game (GAME_STATE + SAVE_DATA, 0x144 bytes).
struct [[gnu::packed]] SaveRecord {
    uint32_t magic;
    // A round is in progress, so the record is worth keeping: `save_reset` clears everything
    // unless this is set. (0x18004248 reads it at +4; it was read at +0x84 here, which meant
    // every start-up wiped the save it had just loaded.)
    uint8_t in_progress;
    uint8_t pad_5[0x1];
    int8_t strokes_by_hole[18];
    uint8_t pad_18[0x20];
    int8_t course;  // signed byte: the course in progress
    uint8_t pad_39[0x14];
    uint8_t clock_shown;  // draw the clock and battery (with the option)
    uint8_t pad_4e[0x36];
    uint8_t byte_84;  // TODO: unknown; the "round in progress" flag it was taken for is at +4
    uint8_t pad_85[0x13];
    uint8_t strokes;  // on the hole in progress
    uint8_t pad_99;
    uint8_t resume_course;
    uint8_t resume_hole;
    uint8_t pad_9c[0x28];
    uint16_t gender;
    uint16_t result;
    uint8_t pad_c8[0x7c];
};
static_assert(offsetof(SaveRecord, course) == 0x38);
static_assert(offsetof(SaveRecord, in_progress) == 0x4);
static_assert(offsetof(SaveRecord, byte_84) == 0x84);
static_assert(offsetof(SaveRecord, strokes) == 0x98);
static_assert(offsetof(SaveRecord, gender) == 0xc4);
static_assert(sizeof(SaveRecord) == 0x144);
inline SaveRecord& save_record() {
    return guest<SaveRecord>(GAME_STATE + game_state::SAVE_DATA);
}

// The synchronous file calls' two-word record: the file handle and the last status.
struct [[gnu::packed]] SimpleFile {
    uint32_t handle;
    uint32_t status;
};
inline SimpleFile& as_simple_file(uint32_t address) {
    return guest<SimpleFile>(address);
}

// The statistics file as written: the two header words and a pointer to the entries.
struct [[gnu::packed]] StatsRecord {
    uint32_t header;
    uint32_t count;
    uint32_t data;
};
inline StatsRecord& as_stats_record(uint32_t address) {
    return guest<StatsRecord>(address);
}

// A node of the C runtime's at-exit list.
struct [[gnu::packed]] AtExitNode {
    uint32_t next, function, argument, dso;
};

// A GL matrix as the game keeps it: sixteen words and an identity flag.
struct [[gnu::packed]] GlMatrix {
    uint32_t m[16];
    uint8_t is_identity;
};
inline GlMatrix& as_matrix(uint32_t address) {
    return guest<GlMatrix>(address);
}
// A draw list head of the renderer (six of them).
struct [[gnu::packed]] DrawList {
    uint32_t next, previous, renderer, index;
};
// An object with the renderer's two rectangles at +0xc and +0x1c.
struct [[gnu::packed]] Rect {
    uint32_t x, y, width, height;
};
struct [[gnu::packed]] RectObject {
    uint32_t width, height;  // the renderer's size
    uint8_t pad_8[0x4];
    Rect rect_a;
    Rect rect_b;
};
static_assert(offsetof(RectObject, rect_b) == 0x1c);
inline RectObject& as_rect_object(uint32_t address) {
    return guest<RectObject>(address);
}

// The device block (0x1801bdd4): shared by the audio device and the statistics writer.
struct [[gnu::packed]] DeviceBlock {
    uint8_t byte_0;
    uint8_t stats_enabled;
    uint8_t pad_2[0xe];
    uint32_t lock;  // the battery lock: taken once, never released
    uint32_t battery_time;
    uint32_t battery_level;  // 0..20
    uint8_t pad_1c[0x4];
    uint32_t stats_buffer;  // -> the bytes being written
};
inline DeviceBlock& device_block() {
    return guest<DeviceBlock>(0x1801'bdd4);
}

// The flags object the app-level code points at (APP2_FLAGS_POINTER).
struct [[gnu::packed]] FlagsObject {
    uint8_t pad_0[0x20];
    uint32_t option_bits;
    uint32_t idle_answer;
    uint8_t idle;
};
inline FlagsObject& flags_object() {
    return guest<FlagsObject>(guest<uint32_t>(APP2_FLAGS_POINTER));
}

// The course loader's block (COURSE_LOADER): a dialog flag and the ground tile store.
struct [[gnu::packed]] CourseLoader {
    uint8_t byte_0;
    uint8_t dialog_shown;
    uint8_t pad_2[0x6];
    uint32_t ground_store;
};
inline CourseLoader& course_loader() {
    return guest<CourseLoader>(COURSE_LOADER);
}

// A pack table entry (16 bytes): the resource's offset word (low 28 bits; bit 31 compressed,
// bits 28..30 the kind), unpacked and packed sizes, and an image's width and height.
struct [[gnu::packed]] PackEntry {
    uint32_t offset_word;
    uint32_t size;
    uint32_t packed;
    int16_t width, height;
};
static_assert(sizeof(PackEntry) == 16);
// A compressed resource's header: the escape code and the coder's parameters, then a table of
// run bytes and the bit stream.
struct [[gnu::packed]] CompressedHeader {
    uint8_t pad_0[0x4];
    uint8_t escape, code_bits, prefix_limit, long_run, offset_bits, table_size;
    uint8_t table[1];
};
inline CompressedHeader& as_compressed(uint32_t address) {
    return guest<CompressedHeader>(address);
}

// The hole's tile map: a byte per tile of the 256×256 map TILE_MAP points at.
inline uint8_t& tile_at(uint32_t x, uint32_t y) {
    return guest_array<uint8_t>(guest<uint32_t>(TILE_MAP))[x + (y << 8)];
}

}  // namespace minigolf::game
