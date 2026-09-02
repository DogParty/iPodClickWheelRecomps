// The game's files and the resources inside them. Every file the game reads is first pulled
// whole into memory as a chain of chunks (FILE_TABLE); a resource pack is such a file plus a
// table of entries — offset, size, kind and whether the bytes are compressed — and reading a
// resource is a seek, then a copy or a decompression into the caller's buffer or the pack's
// own scratch buffer. Allocations that must be freed later go through a tracked heap with
// guard bytes.
//
// Long because it is two layers that only exist for each other: files in memory, and the
// resource packs built on them. Neither has a caller that wants one without the other.
#include "resources.h"

#include "async_request.h"
#include "calling.h"
#include "draw.h"
#include "framework/graphics.h"
#include "game_state.h"
#include "libc.h"
#include "records.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "state.h"
#include "strings.h"

#include <algorithm>
#include <vector>

namespace minigolf::game {

void tracked_free(uint32_t memory);
void tracked_free_all();
void pack_close(PackRecord& pack_at);
uint32_t resource_scratch_load(PackRecord& pack_at, uint32_t id);
uint32_t file_open(uint32_t name);
uint32_t file_read(FileEntry& at, uint32_t destination, uint32_t bytes);
uint32_t file_cursor(FileEntry& at);
uint32_t tracked_allocate(uint32_t bytes);

namespace {

constexpr uint32_t KIND_SCRATCH = 2;  // a resource delivered in the pack's scratch buffer

constexpr uint32_t TITLE_PACK_NAME = 0x1800'fdc4;  // "jdmgsheets"

constexpr uint32_t LITERAL_ZERO = 0x1800'8818, LITERAL_ZERO_DOT = 0x1800'865c,
                   LITERAL_DOT = 0x1800'8660;
// An entry: 16 bytes.

// The tracked heap: a registry of what was allocated (memory, size) and a 0xcd guard byte
// either side of each block. The registry itself is host state — the original kept it at
// 0x1801a510 / 0x18038a74, which nothing else reads.
struct TrackedBlock {
    uint32_t memory, bytes;
};
std::vector<TrackedBlock> tracked;
constexpr uint32_t GUARD = 0xcd;
// Which files a course owns: a table by pack row and file index, a flag byte at +8.
constexpr uint32_t TEXTURE_SIZES = 0x1803'fc08;  // bytes uploaded per texture name
constexpr uint32_t GROUND_PALETTE_BYTES = 0x400;

struct Entry {
    uint32_t file, offset, size, packed;
    bool compressed;
    uint32_t kind;
};

Entry entry_find(PackRecord& pack_at, uint32_t id, uint32_t assert_at) {
    const uint32_t count_a = pack_at.count_a;
    uint32_t file = pack_at.file_a, table = pack_at.table_a;
    if (static_cast<int32_t>(count_a) <= static_cast<int32_t>(id)) {
        file = pack_at.file_b;
        table = pack_at.table_b;
        id -= count_a;
    }
    if (file == 0 || table == 0) {
        assert_trap(assert_at);
    }
    const PackEntry& at = table_entry<PackEntry>(table, id);
    const uint32_t word = at.offset_word;
    const uint32_t flags = word >> 28;
    return {file, word & 0x0fff'ffffu, at.size, at.packed, (flags & 8) != 0, flags & 7};
}

// --- The decompressor's bit stream ------------------------------------------------------------
// A stream object on the decompressor's stack: where it writes, where it reads, the bits
// still unread of the current byte, the byte count written, then the stream's parameters.
}  // namespace

// 0x18014ee0 — the next bit of the stream, high bit first.
uint32_t bit_read(BitStream& s) {
    if (s.bits == 0) {
        const uint32_t in = s.in;
        s.in = in + 1;
        s.current = guest<uint8_t>(in);
        s.bits = 8;
    }
    const uint32_t bits = s.bits - 1;
    s.bits = bits;
    return (s.current >> bits) & 1;
}

// 0x18009708 — `count` bits (at most the rest of the current byte plus one more), appended
// below `prefix`.
uint32_t bits_read(BitStream& s, uint32_t count, uint32_t prefix) {
    const uint32_t bits = s.bits;
    if (static_cast<int32_t>(bits) >= static_cast<int32_t>(count)) {
        s.bits = bits - count;
        const uint32_t value = (s.current >> (bits - count)) & ((1u << count) - 1);
        return value | (prefix << count);
    }
    // The rest of this byte, then the start of the next.
    prefix = (s.current & ((1u << bits) - 1)) | (prefix << bits);
    const uint32_t remaining = count - bits;
    const uint32_t in = s.in;
    s.in = in + 1;
    const uint32_t next = guest<uint8_t>(in);
    s.bits = 8 - remaining;
    s.current = next;
    return (next >> (8 - remaining)) | (prefix << remaining);
}

// 0x18014e9c — a number coded as its bit length in unary (ones up to PREFIX_LIMIT, then a
// zero) followed by the bits below its top one.
uint32_t prefix_read(BitStream& s) {
    uint32_t length = 1;
    while (bit_read(s) != 0 &&
           static_cast<int32_t>(s.prefix_limit) != static_cast<int32_t>(++length)) {
    }
    return bits_read(s, length - 1, 1);
}

// 0x180152d0 — one byte out.
void byte_write(BitStream& s, uint32_t byte) {
    const uint32_t out = s.out;
    guest<uint8_t>(out) = static_cast<uint8_t>(byte);
    s.out = out + 1;
    s.written = s.written + 1;
}

// 0x18006f7c — unpack a resource. The header gives an escape code, the width of the codes,
// and the limits of the prefix codes; then each code is a literal byte unless it is the
// escape, which introduces a back-reference (length and distance), a run of one byte (from
// a small table or spelled out), a literal equal to the escape itself with a new escape to
// follow, or the end. Returns the bytes written.
uint32_t decompress(uint32_t source, uint32_t destination) {
    GuestScratch frame(4 * 4 + 0x50);
    if (source == 0 || destination == 0) {
        assert_trap(0x18006f8cu);
    }
    BitStream& s = as_stream(frame.at(4));
    const CompressedHeader& header = as_compressed(source);
    s.escape = header.escape;
    s.code_bits = header.code_bits;
    s.prefix_limit = header.prefix_limit;
    s.long_run = header.long_run;
    s.offset_bits = header.offset_bits;
    const uint32_t table_size = header.table_size;
    for (uint32_t i = 0; i < table_size; ++i) {
        s.table[i] = guest_array<uint8_t>(source + 10)[i];
    }
    s.in = source + 10 + table_size;
    s.bits = 0;
    s.written = 0;
    s.out = destination;
    const uint32_t code_bits = s.code_bits;

    const auto literal_after = [&](uint32_t prefix) {
        byte_write(s, bits_read(s, 8 - code_bits, prefix));
    };
    const auto copy_back = [&](uint32_t high, uint32_t count) {
        const uint32_t low = bits_read(s, 8, 0);
        uint32_t from = s.out + low - 0x100 - (high << 8);
        for (uint32_t i = 0; i < count; ++i, ++from) {
            byte_write(s, guest<uint8_t>(from));
        }
    };
    for (;;) {
        const uint32_t code = bits_read(s, code_bits, 0);
        if (code != s.escape) {
            literal_after(code);
            continue;
        }
        const uint32_t length = prefix_read(s);
        s.run = length;
        if (static_cast<int32_t>(length) >= 2) {  // a back-reference, or the end
            const uint32_t distance_code = prefix_read(s);
            if (distance_code == s.long_run * 2 - 1) {
                return s.written;
            }
            copy_back(bits_read(s, s.offset_bits, distance_code - 1), length + 1);
            continue;
        }
        if (bit_read(s) == 0) {  // a short back-reference within the last 256 bytes
            copy_back(0, length + 1);
            continue;
        }
        if (bit_read(s) == 0) {  // the escape itself as a literal, and a new escape code
            const uint32_t old_escape = s.escape;
            s.escape = bits_read(s, code_bits, 0);
            literal_after(old_escape);
            continue;
        }
        // A run of one byte.
        uint32_t run = prefix_read(s);
        if (static_cast<int32_t>(run) >= static_cast<int32_t>(s.long_run)) {
            const uint32_t low = bits_read(s, 9 - s.prefix_limit, run) & 0xff;
            run = low + (prefix_read(s) << 8) - 0x100;
        }
        s.run = run;
        const uint32_t which = prefix_read(s);
        const uint32_t byte = static_cast<int32_t>(which) < 0x20 ? s.table[which - 1]
                                                                 : (bits_read(s, 3, which) & 0xff);
        for (uint32_t i = 0; i <= s.run; ++i) {
            byte_write(s, byte);
        }
    }
}

// --- Files in memory ----------------------------------------------------------------------------

// 0x180024d8 — the file of that name, its cursor at the start; 0 if there is none.
uint32_t file_open(uint32_t name) {
    for (uint32_t i = 0; i < FILE_COUNT; ++i) {
        const uint32_t entry = FILE_TABLE + i * FILE_SIZE;
        FileEntry& at = as_file(entry);
        if (libc::string_compare(name, as_file_kind(at.name).name) != 0) {
            continue;
        }
        at.cursor = at.chunks[0];
        at.offset = 0;
        at.chunk = 0;
        return entry;
    }
    return 0;
}

// 0x1800254c — `bytes` from the cursor, across chunk boundaries. Returns the bytes copied.
uint32_t file_read(FileEntry& at, uint32_t destination, uint32_t bytes) {
    if (destination == 0) {
        assert_trap(0x18002558u);
    }
    uint32_t read = 0;
    uint32_t chunk = at.chunk;
    while (bytes != 0) {
        const uint32_t offset = at.offset;
        const uint32_t size = at.sizes[chunk];
        if (static_cast<int32_t>(offset + bytes - 1) <= static_cast<int32_t>(size)) {
            libc::memory_copy(destination, at.cursor, bytes);
            at.cursor = at.cursor + bytes;
            at.offset = offset + bytes;
            return read + bytes;
        }
        const uint32_t part = size - offset;
        libc::memory_copy(destination, at.cursor, part);
        ++chunk;
        at.cursor = at.chunks[chunk];
        at.offset = 0;
        at.chunk = chunk;
        bytes -= part;
        destination += part;
        read += part;
    }
    return read;
}

// 0x180024c8 — where the cursor points.
uint32_t file_cursor(FileEntry& at) {
    return at.cursor;
}

// 0x1800261c — the cursor to `offset` from the start (the only origin supported).
void file_seek(FileEntry& at, uint32_t offset, uint32_t origin) {
    if (origin != 0) {
        assert_trap(0x18002624u);
    }
    if (as_file_kind(at.name).size >= offset) {
        uint32_t chunk = 0, left = offset;
        while (left != 0) {
            const uint32_t size = at.sizes[chunk];
            if (static_cast<int32_t>(size) < static_cast<int32_t>(left)) {
                left -= size;
                ++chunk;
                continue;
            }
            at.offset = left;
            at.chunk = chunk;
            at.cursor = at.chunks[chunk] + left;
            break;
        }
    }
    if (offset == 0) {
        at.cursor = at.chunks[0];
        at.offset = 0;
        at.chunk = 0;
    }
}

// --- Resources ----------------------------------------------------------------------------------

// 0x18008c44 — a resource's unpacked size.
uint32_t resource_size(PackRecord& pack_at, uint32_t id) {
    return entry_find(pack_at, id, 0x18008c4cu).size;
}

// 0x18008868 — a resource into `destination` (no larger than `limit` when one is given),
// unpacking it if it is compressed. A resource of the scratch kind lands in the pack's
// scratch buffer instead, and that buffer is returned; otherwise 1.
uint32_t resource_load(PackRecord& pack_at, uint32_t id, uint32_t destination, uint32_t limit) {
    const Entry e = entry_find(pack_at, id, 0x18008878u);
    if ((destination != 0 && static_cast<int32_t>(e.size) > static_cast<int32_t>(limit)) ||
        static_cast<int32_t>(pack_at.largest) < static_cast<int32_t>(e.size)) {
        assert_trap(0x180088e4u);
    }
    file_seek(as_file(e.file), e.offset, 0);
    const uint32_t scratch = pack_at.scratch;
    if (scratch == 0) {
        assert_trap(0x18008910u);
    }
    const uint32_t target = e.kind == KIND_SCRATCH ? scratch : destination;
    if (e.compressed) {
        if (decompress(file_cursor(as_file(e.file)), target) != e.size) {
            assert_trap(0x18008954u);
        }
    } else if (file_read(as_file(e.file), target, e.size) != e.size) {
        assert_trap(0x18008954u);
    }
    return e.kind == KIND_SCRATCH ? scratch : 1;
}

// 0x18008af8 — a resource's bytes: into `destination`, into a fresh tracked allocation when
// `allocate` is 1, or into the pack's scratch buffer when there is no destination. Compressed
// bytes are first read into the scratch buffer past where the result will go.
uint32_t resource_open(PackRecord& pack_at, uint32_t id, uint32_t destination, uint32_t allocate) {
    const Entry e = entry_find(pack_at, id, 0x18008b0cu);
    file_seek(as_file(e.file), e.offset, 0);
    const uint32_t aligned = (e.size + 3) & ~3u;
    if (static_cast<int32_t>(pack_at.largest) < static_cast<int32_t>(aligned)) {
        assert_trap(0x18008b84u);
    }
    if (allocate == 1) {
        destination = tracked_allocate(e.size);
    }
    const uint32_t scratch = pack_at.scratch;
    if (!e.compressed) {
        if (destination == 0) {
            destination = scratch;
        }
        if (file_read(as_file(e.file), destination, e.size) != e.size) {
            assert_trap(0x18008c38u);
        }
        return destination;
    }
    const uint32_t packed_at = scratch + aligned;
    if (static_cast<int32_t>(aligned + e.packed) > static_cast<int32_t>(pack_at.largest)) {
        assert_trap(0x18008bbcu);
    }
    if (file_read(as_file(e.file), packed_at, e.packed) != e.packed) {
        assert_trap(0x18008bd8u);
    }
    const uint32_t target = destination == 0 ? scratch : destination;
    if (decompress(packed_at, target) != e.size) {
        assert_trap(0x18008c14u);
    }
    return target;
}

// 0x180089a8 — a resource of the scratch kind into the pack's scratch buffer; 0 for any other.
uint32_t resource_scratch_load(PackRecord& pack_at, uint32_t id) {
    const Entry e = entry_find(pack_at, id, 0x180089b4u);
    if (static_cast<int32_t>(pack_at.largest) < static_cast<int32_t>(e.size)) {
        assert_trap(0x18008a20u);
    }
    file_seek(as_file(e.file), e.offset, 0);
    const uint32_t scratch = pack_at.scratch;
    if (scratch == 0) {
        assert_trap(0x18008a3cu);
    }
    if (e.kind != KIND_SCRATCH) {
        return 0;
    }
    if (e.compressed) {
        if (decompress(file_cursor(as_file(e.file)), scratch) != e.size) {
            assert_trap(0x18008a80u);
        }
    } else if (file_read(as_file(e.file), scratch, e.size) != e.size) {
        assert_trap(0x18008a80u);
    }
    return scratch;
}

// 0x1800906c — an image's header (nine words from texture index to height) from a resource,
// its width and height copied into the image record, and the variant to draw noted.
uint32_t image_apply(ImageRecord& image, uint32_t variant, PackRecord& pack_at, uint32_t id) {
    if (variant >= 3) {
        assert_trap(0x18009084u);
    }
    resource_load(pack_at, id, field_address(image, offsetof(ImageRecord, texture_index)), 0x24);
    image.width = image.cell_u;  // the header's words, as loaded
    image.height = image.cell_height;
    image.variant = static_cast<uint8_t>(variant);
    return 1;
}

// 0x18008fac — forget an image's texture.
void texture_release(ImageRecord& image) {
    image.texture_name = 0;
}

// 0x1800fe28 — memory that is freed later: registered with its size, with a guard byte
// before and after. Returns 0 when the heap is out.
uint32_t tracked_allocate(uint32_t bytes) {
    const uint32_t block = libc::heap_allocate(bytes + 8);
    if (block == 0) {
        return 0;
    }
    tracked.push_back({block + 4, bytes});
    guest<uint8_t>(block + 3) = GUARD;
    guest<uint8_t>(block + bytes + 4) = GUARD;
    return block + 4;
}

// 0x1800cd8c — give tracked memory back, if it is in the registry.
void tracked_free(uint32_t memory) {
    if (memory == 0) {
        return;
    }
    const auto entry = std::find_if(tracked.begin(), tracked.end(),
                                    [memory](const TrackedBlock& t) { return t.memory == memory; });
    if (entry == tracked.end()) {
        return;
    }
    libc::heap_free(memory - 4);
    entry->memory = 0;
}

// 0x180105e4 — one 80×60 ground tile into the ground store at tile column x, row y (720 bytes
// a row, after the palette).
void ground_tile_blit(uint32_t pixels, uint32_t x, uint32_t y) {
    if (pixels == 0) {
        assert_trap(0x180105f0u);
    }
    uint32_t to = course_loader().ground_store + y * 0x2d0 + x + 0x400;
    for (uint32_t row = 0; row < 0x3c; ++row, to += 0x2d0, pixels += 0x50) {
        libc::memory_copy(to, pixels, 0x50);
    }
    if (to - 0x280 > 0x180c'4164u) {
        assert_trap(0x18010644u);
    }
}

// --- Files, packs and images at the level above ---------------------------------------------

// 0x1800409c — free the memory of the files the course owns (or every file when `all`), and
// with `all` the main pack too, and everything the tracked heap still holds.
void files_release(uint32_t all) {
    for (uint32_t i = 0; i < FILE_COUNT; ++i) {
        FileEntry& at = as_file(FILE_TABLE + i * FILE_SIZE);
        const FileKind& kind = file_kind(at.text, at.entry);
        if ((kind.owned_by_course | all) == 0) {
            continue;
        }
        for (uint32_t chunk = 0; chunk < file::CHUNK_LIMIT; ++chunk) {
            if (at.chunks[chunk] != 0) {
                tracked_free(at.chunks[chunk]);
                at.chunks[chunk] = 0;
                at.sizes[chunk] = 0;
            }
        }
    }
    if (all != 0) {
        pack_close(as_pack(ld32(GAME_STATE + game_state::PACK_HANDLE + 4)));
        tracked_free_all();
    }
}

// 0x1800881c — close a pack's files and free it.
void pack_close(PackRecord& pack_at) {
    pack_at.file_b = 0;
    pack_at.file_a = 0;
    tracked_free(address_of(pack_at));
}

// 0x1800f7ac — everything the tracked heap holds, freed.
void tracked_free_all() {
    for (size_t i = 0; i < tracked.size(); ++i) {  // tracked_free may not grow the registry
        if (tracked[i].memory != 0) {
            tracked_free(tracked[i].memory);
        }
    }
    tracked.clear();
}

// 0x18008ac4 / 0x18008a90 — an image resource's width and height, from its entry.
uint32_t entry_width(PackRecord& pack_at, uint32_t id) {
    const Entry e = entry_find(pack_at, id, 0x18008accu);
    (void)e;
    const uint32_t count_a = pack_at.count_a;
    const uint32_t table = static_cast<int32_t>(count_a) > static_cast<int32_t>(id)
                               ? pack_at.table_a
                               : pack_at.table_b;
    const uint32_t index =
        static_cast<int32_t>(count_a) > static_cast<int32_t>(id) ? id : id - count_a;
    return static_cast<uint32_t>(static_cast<int32_t>(table_entry<PackEntry>(table, index).width));
}

uint32_t entry_height(PackRecord& pack_at, uint32_t id) {
    const Entry e = entry_find(pack_at, id, 0x18008a98u);
    (void)e;
    const uint32_t count_a = pack_at.count_a;
    const uint32_t table = static_cast<int32_t>(count_a) > static_cast<int32_t>(id)
                               ? pack_at.table_a
                               : pack_at.table_b;
    const uint32_t index =
        static_cast<int32_t>(count_a) > static_cast<int32_t>(id) ? id : id - count_a;
    return static_cast<uint32_t>(static_cast<int32_t>(table_entry<PackEntry>(table, index).height));
}

// 0x180090c8 — a texture from pixels: the image record takes the texture name, width, height
// and variant, the texture is bound, its size (plus the palette) noted per name, and the
// pixels uploaded as a paletted image.
uint32_t texture_from_pixels(ImageRecord& image, uint32_t variant, uint32_t width, uint32_t height,
                             uint32_t pixels, uint32_t texture) {
    GuestScratch frame(4 * 8);
    if (width == 0 || height == 0 || pixels == 0) {
        assert_trap(0x180090dcu);
    }
    image.texture_name = texture;
    image.width = width;
    image.height = height;
    image.variant = static_cast<uint8_t>(variant);
    image.pixels = pixels;
    gfx::bind_texture(gfx::TextureTarget::Texture2D, texture);
    gfx::error();
    const uint32_t bytes = width * height + GROUND_PALETTE_BYTES;
    const uint32_t known = guest_array<uint32_t>(TEXTURE_SIZES)[texture];
    if (known != 0 && known < bytes) {
        assert_trap(0x18009154u);
    }
    guest_array<uint32_t>(TEXTURE_SIZES)[texture] = bytes;
    gfx::compressed_texture_image(gfx::TextureTarget::Texture2D, 0, gfx::PixelFormat::Palette8Rgba8,
                                  width, height, 0, bytes, pixels);
    gfx::error();
    return 1;
}

// 0x18008fcc — an image from a pack resource of the scratch kind: its pixels land in the
// pack's scratch buffer and become a texture of the given name. Returns 1 on success.
uint32_t image_from_resource(ImageRecord& image, uint32_t variant, PackRecord& pack_at, uint32_t id,
                             uint32_t texture) {
    if (variant >= 3) {
        assert_trap(0x18008fe8u);
    }
    const uint32_t pixels = resource_scratch_load(pack_at, id);
    if (pixels == 0) {
        return 0;
    }
    const uint32_t width = entry_width(pack_at, id), height = entry_height(pack_at, id);
    texture_from_pixels(image, variant, width, height, pixels, texture);
    return image.texture_name != 0 ? 1 : 0;
}

// 0x1800853c — a pack's second pair of files, "<name>0.<suffix>" for the table (whose size
// must be what the first header said) and "<name>.<suffix>" for the data. Returns 1 when
// both opened.
uint32_t pack_tables_open(PackRecord& pack_at, uint32_t suffix) {
    GuestScratch frame(4 * 4 + 0x38);
    if (suffix == 0) {
        assert_trap(0x18008550u);
    }
    if (string_length(suffix) != 2) {
        assert_trap(0x18008570u);
    }
    const uint32_t path = frame.at(4), size_at = frame.at(0x34);
    guest<uint8_t>(path) = 0;
    string_copy(path, field_address(pack_at, offsetof(PackRecord, name)));
    string_append(path, LITERAL_ZERO_DOT);
    string_append(path, suffix);
    const uint32_t table_file = file_open(path);
    if (table_file == 0) {
        return 0;
    }
    if (pack_at.file_b != 0) {
        (void)pack_at.file_b;  // closing a file in memory is nothing
    }
    if (file_read(as_file(table_file), size_at, 4) != 4 || guest<uint32_t>(size_at) < 4 ||
        pack_at.count_b_bytes != guest<uint32_t>(size_at)) {
        assert_trap(0x180085f8u);
    }
    pack_at.table_b = file_cursor(as_file(table_file));
    (void)table_file;  // closing a file in memory is nothing
    string_copy(path, field_address(pack_at, offsetof(PackRecord, name)));
    string_append(path, LITERAL_DOT);
    string_append(path, suffix);
    pack_at.file_b = file_open(path);
    return pack_at.file_b != 0 ? 1 : 0;
}

// 0x18008664 — open a pack by name: "<name>0" holds the header (the largest resource, the
// table's size, and the size of a second table) and the first table; "<name>" the data. With
// a suffix, the second pair of files is opened too.
uint32_t pack_open(uint32_t name, uint32_t unused, uint32_t suffix) {
    (void)unused;
    GuestScratch frame(4 * 4 + 0x40);
    if (name == 0 || string_length(name) <= 0 || string_length(name) > 0x2e) {
        assert_trap(0x18008678u);
    }
    const uint32_t path = frame.at(4), table_size_at = frame.at(0x3c),
                   second_size_at = frame.at(0x38);
    guest<uint32_t>(table_size_at) = 0;
    guest<uint32_t>(second_size_at) = 0;
    guest<uint8_t>(path) = 0;
    const uint32_t handle = tracked_allocate(sizeof(PackRecord));
    libc::memory_clear(handle, sizeof(PackRecord));
    PackRecord& pack_at = as_pack(handle);
    string_copy(field_address(pack_at, offsetof(PackRecord, name)), name);
    string_copy(path, name);
    string_append(path, LITERAL_ZERO);
    const uint32_t header_file = file_open(path);
    if (header_file == 0) {
        assert_trap(0x1800870cu);
    }
    if (file_read(as_file(header_file), field_address(pack_at, offsetof(PackRecord, largest)), 4) !=
            4 ||
        pack_at.largest < 4) {
        assert_trap(0x18008738u);
    }
    if (file_read(as_file(header_file), table_size_at, 4) != 4 ||
        guest<uint32_t>(table_size_at) < 4) {
        assert_trap(0x18008764u);
    }
    pack_at.count_a = guest<uint32_t>(table_size_at) >> 4;
    if (file_read(as_file(header_file), second_size_at, 4) != 4) {
        assert_trap(0x18008788u);
    }
    pack_at.table_a = file_cursor(as_file(header_file));
    if (pack_at.table_a == 0) {
        assert_trap(0x180087a0u);
    }
    pack_at.table_a_bytes = guest<uint32_t>(table_size_at);
    (void)header_file;  // closing a file in memory is nothing
    if (static_cast<int32_t>(guest<uint32_t>(second_size_at)) > 0 && suffix != 0) {
        if (guest<uint32_t>(second_size_at) < 4) {
            assert_trap(0x180087d0u);
        }
        pack_at.count_b_bytes = guest<uint32_t>(second_size_at);
        pack_tables_open(pack_at, suffix);
    }
    string_copy(path, field_address(pack_at, offsetof(PackRecord, name)));
    pack_at.file_a = file_open(path);
    if (pack_at.file_a == 0) {
        assert_trap(0x1800880cu);
    }
    return handle;
}

// 0x18018734 — construct an array of objects: `count` of `size` bytes, allocated unless
// `memory` is given, each cleared (when `clear`) and run through `constructor`; two words
// before the array keep the size and count for the destructor.
uint32_t array_construct(uint32_t memory, uint32_t allocator, uint32_t size, uint32_t header,
                         uint32_t count, uint32_t destructor, ElementConstructor constructor,
                         uint32_t clear) {
    (void)destructor;
    const uint32_t bytes = size * count;
    uint32_t at = memory;
    if (memory == 0 && header == 0) {
        at = 0;
    } else if (memory == 0) {
        const uint32_t total = bytes + header;
        const uint32_t memory_at =
            allocator == 0 ? operator_new(total) : call_indirect(allocator, {total});
        if (memory_at == 0) {
            return 0;
        }
        at = memory_at + header;
    }
    if (at == 0) {
        return 0;
    }
    if (header != 0) {
        guest<uint32_t>(at - 8) = size;  // the array header: element size and count
        guest<uint32_t>(at - 4) = count;
    }
    if (clear != 0) {
        libc::memory_clear(at, bytes);
    }
    if (constructor != nullptr) {
        uint32_t element = at;
        for (uint32_t i = 0; i < count; ++i, element += size) {
            constructor(element);
        }
    }
    return at;
}

// 0x1800fd0c — open the title pack ("jdmgsheets", stored at PACK_TITLE) and load its four
// images, textures 2..5; the files it opened are released on the way out.
void title_pack_open() {
    {
        const uint32_t handle = pack_open(TITLE_PACK_NAME, 0, 0);
        game_state_block().pack_title = handle;
        if (handle == 0) {
            assert_trap(0x1800fd30u);
        }
        PackRecord& pack = as_pack(handle);
        pack.largest = game_state_block().word_28;
        pack.scratch = GROUND_TILE_SCRATCH;
        for (uint32_t i = 0; i < 4; ++i) {
            image_from_resource(as_image(TITLE_IMAGE + (2 + i) * TEXTURE_TABLE_STRIDE), 1,
                                as_pack(game_state_block().pack_title), i, 2 + i);
        }
    }
    files_release(0);
}

// 0x18008db4 / 0x18008dcc — an object's two rectangles (+0xc and +0x1c): x, y, width, height.
void object_rect_a_set(RectObject& object, uint32_t x, uint32_t y, uint32_t width,
                       uint32_t height) {
    object.rect_a = {x, y, width, height};
}

void object_rect_b_set(RectObject& object, uint32_t x, uint32_t y, uint32_t width,
                       uint32_t height) {
    object.rect_b = {x, y, width, height};
}

}  // namespace minigolf::game
