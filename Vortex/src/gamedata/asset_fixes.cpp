// The shipped-data corrections. See asset_fixes.h for what is corrected and why.
#include "gamedata/asset_fixes.h"

#include <cstdio>

namespace vortex::gamedata {

namespace {

// The texture pack's layout, established by reading it against the game's own loader: a count,
// a word the loader does not use, then one 32-bit size per entry, then the entries back to back.
// Each entry is a 16-byte header — width, height, format, unused — followed by its pixels.
constexpr uint32_t PACK_COUNT_OFFSET = 0;
constexpr uint32_t PACK_DIRECTORY_OFFSET = 8;
constexpr uint32_t ENTRY_HEADER_BYTES = 16;
constexpr uint32_t ENTRY_WIDTH_OFFSET = 0;
constexpr uint32_t ENTRY_HEIGHT_OFFSET = 4;
// Byte 8 is the format code (0 plain, 9 paletted, …) and byte 9 the bits a pixel, which is what
// the uploader at 0x18015628 dispatches on (`ldrb r3,[ip,#9] / cmp r3,#8 / … / cmp r3,#0x10`).
constexpr uint32_t ENTRY_BITS_PER_PIXEL_OFFSET = 9;
constexpr uint8_t BITS_PER_PIXEL_8 = 8;
constexpr uint8_t BITS_PER_PIXEL_16 = 16;

// A sane bound on the entry count, so a file that is not the pack cannot make this allocate or
// walk on the strength of a garbage word. The pack ships with 46.
constexpr uint32_t MOST_ENTRIES = 4096;

bool& enabled() {
    static bool on = true;
    return on;
}

uint32_t read_u32(const std::vector<uint8_t>& bytes, size_t at) {
    return static_cast<uint32_t>(bytes[at]) | static_cast<uint32_t>(bytes[at + 1]) << 8 |
           static_cast<uint32_t>(bytes[at + 2]) << 16 | static_cast<uint32_t>(bytes[at + 3]) << 24;
}

// Is this the texture pack? Answered from the file's own structure rather than from its name
// alone: the directory has to describe the file exactly — every size accounted for, nothing left
// over — which a file that merely shares the name cannot do by accident.
bool is_texture_pack(const std::vector<uint8_t>& bytes, uint32_t& count) {
    if (bytes.size() < PACK_DIRECTORY_OFFSET + 4) {
        return false;
    }
    count = read_u32(bytes, PACK_COUNT_OFFSET);
    if (count == 0 || count > MOST_ENTRIES) {
        return false;
    }
    const size_t directory_end = PACK_DIRECTORY_OFFSET + size_t{count} * 4;
    if (bytes.size() < directory_end) {
        return false;
    }
    size_t total = directory_end;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t size = read_u32(bytes, PACK_DIRECTORY_OFFSET + size_t{i} * 4);
        if (size > bytes.size()) {
            return false;
        }
        total += size;
        if (total > bytes.size()) {
            return false;
        }
    }
    return total == bytes.size();
}

}  // namespace

void set_asset_corrections(bool on) {
    enabled() = on;
}

bool asset_corrections_enabled() {
    return enabled();
}

uint32_t correct_asset(const std::string& name, std::vector<uint8_t>& bytes) {
    if (!enabled()) {
        return 0;
    }
    uint32_t count = 0;
    if (!is_texture_pack(bytes, count)) {
        return 0;
    }

    uint32_t corrected = 0;
    size_t entry = PACK_DIRECTORY_OFFSET + size_t{count} * 4;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t size = read_u32(bytes, PACK_DIRECTORY_OFFSET + size_t{i} * 4);
        if (size < ENTRY_HEADER_BYTES) {
            entry += size;  // too small to hold a header; not ours to judge
            continue;
        }
        const uint32_t width = read_u32(bytes, entry + ENTRY_WIDTH_OFFSET);
        const uint32_t height = read_u32(bytes, entry + ENTRY_HEIGHT_OFFSET);
        uint8_t& bits = bytes[entry + ENTRY_BITS_PER_PIXEL_OFFSET];
        const uint64_t pixels = uint64_t{width} * height;
        const uint32_t data = size - ENTRY_HEADER_BYTES;
        // The label says two bytes a pixel and the entry holds exactly one. Nothing else is
        // touched: an entry whose length matches its label is right, and one that matches
        // neither is a fault this does not understand and must not paper over.
        if (bits == BITS_PER_PIXEL_16 && pixels != 0 && data == pixels) {
            bits = BITS_PER_PIXEL_8;
            ++corrected;
        }
        entry += size;
    }
    if (corrected != 0) {
        // Said out loud, once per file: this is the port drawing something other than what the
        // shipped bytes ask for, and a reader of the log should see that it happened.
        std::fprintf(stderr,
                     "%s: corrected %u texture header(s) that declared 16 bits a pixel over "
                     "8-bit data (src/gamedata/asset_fixes.h)\n",
                     name.c_str(), corrected);
    }
    return corrected;
}

}  // namespace vortex::gamedata
