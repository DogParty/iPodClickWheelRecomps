// The shipped-data correction: that it fixes the mislabelled texture headers and nothing else.
//
// The defect and the rule are in src/gamedata/asset_fixes.h. What matters here is the rule's
// edges, because this code rewrites the player's game data: an entry whose length agrees with
// its label must come through untouched, a file that is not the pack must come through
// untouched, and a file that merely looks like it must not send the walk off the end.
#include "gamedata/asset_fixes.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void put_u32(std::vector<uint8_t>& bytes, uint32_t value) {
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
    bytes.push_back(static_cast<uint8_t>(value >> 16));
    bytes.push_back(static_cast<uint8_t>(value >> 24));
}

// One entry: a 16-byte header (width, height, {format code, bits a pixel, 0, 0}, unused) and
// `data` bytes of pixels.
struct Entry {
    uint32_t width, height;
    uint8_t code, bits;
    uint32_t data;
};

constexpr uint32_t HEADER = 16;
constexpr uint32_t BITS_AT = 9;

std::vector<uint8_t> pack(const std::vector<Entry>& entries) {
    std::vector<uint8_t> bytes;
    put_u32(bytes, static_cast<uint32_t>(entries.size()));
    put_u32(bytes, 0);  // the word the game's loader does not use
    for (const Entry& e : entries) {
        put_u32(bytes, HEADER + e.data);
    }
    for (const Entry& e : entries) {
        put_u32(bytes, e.width);
        put_u32(bytes, e.height);
        bytes.push_back(e.code);
        bytes.push_back(e.bits);
        bytes.push_back(0);
        bytes.push_back(0);
        put_u32(bytes, 0);
        bytes.insert(bytes.end(), e.data, 0xab);
    }
    return bytes;
}

// Where entry `index`'s header begins, for reading the bits-a-pixel back out.
size_t entry_at(const std::vector<Entry>& entries, size_t index) {
    size_t at = 8 + entries.size() * 4;
    for (size_t i = 0; i < index; ++i) {
        at += HEADER + entries[i].data;
    }
    return at;
}

void test_the_mislabelled_entry_is_corrected() {
    // The shipped shape: 32x32 declared at 16 bits a pixel over 1024 bytes, which is 8.
    const std::vector<Entry> entries = {
        {32, 32, 0, 16, 32 * 32},      // mislabelled — the ENTER NAME glyphs
        {256, 192, 0, 16, 256 * 192 * 2},  // honest 16-bit, and much larger
        {128, 128, 0, 8, 128 * 128},       // honest 8-bit
    };
    std::vector<uint8_t> bytes = pack(entries);
    check(vortex::gamedata::correct_asset("tex", bytes) == 1, "exactly one entry is corrected");
    check(bytes[entry_at(entries, 0) + BITS_AT] == 8, "the mislabelled entry is now 8-bit");
    check(bytes[entry_at(entries, 1) + BITS_AT] == 16, "a true 16-bit entry is left alone");
    check(bytes[entry_at(entries, 2) + BITS_AT] == 8, "an 8-bit entry is left alone");
}

// A paletted entry carries a 1024-byte palette as well as its pixels, so its length is *not*
// its pixel count — and it must not be read as a mislabelled one.
void test_a_paletted_entry_is_left_alone() {
    const std::vector<Entry> entries = {{400, 200, 9, 8, 400 * 200 + 1024}};
    std::vector<uint8_t> bytes = pack(entries);
    check(vortex::gamedata::correct_asset("tex", bytes) == 0, "a paletted entry is not corrected");
    check(bytes[entry_at(entries, 0) + BITS_AT] == 8, "and keeps its label");
}

// An entry that matches neither label is a fault this does not understand; leaving it is the
// only honest answer.
void test_an_entry_matching_neither_label_is_left_alone() {
    const std::vector<Entry> entries = {{32, 32, 0, 16, 777}};
    std::vector<uint8_t> bytes = pack(entries);
    check(vortex::gamedata::correct_asset("tex", bytes) == 0, "an unexplained length is not touched");
}

void test_other_files_are_untouched() {
    std::vector<uint8_t> text = {'h', 'e', 'l', 'l', 'o'};
    const std::vector<uint8_t> before = text;
    check(vortex::gamedata::correct_asset("text.strings", text) == 0, "a text file is not a pack");
    check(text == before, "and is not modified");

    std::vector<uint8_t> empty;
    check(vortex::gamedata::correct_asset("tex", empty) == 0, "an empty file is not a pack");
}

// A file whose first words happen to read as a huge count or a size past its end must be
// refused, not walked. This is the player's own data arriving from disk.
void test_a_file_that_only_looks_like_a_pack_is_refused() {
    std::vector<uint8_t> huge;
    put_u32(huge, 0xffffffffu);  // a count no pack has
    put_u32(huge, 0);
    huge.resize(64, 0);
    check(vortex::gamedata::correct_asset("tex", huge) == 0, "an absurd entry count is refused");

    std::vector<uint8_t> overrun;
    put_u32(overrun, 1);
    put_u32(overrun, 0);
    put_u32(overrun, 0x1000);  // one entry, far larger than the file
    overrun.resize(32, 0);
    check(vortex::gamedata::correct_asset("tex", overrun) == 0, "a size past the end is refused");

    // Right shape, one byte too long: the directory must account for the file exactly.
    const std::vector<Entry> entries = {{4, 4, 0, 16, 16}};
    std::vector<uint8_t> trailing = pack(entries);
    trailing.push_back(0);
    check(vortex::gamedata::correct_asset("tex", trailing) == 0, "a file with a tail is refused");
}

void test_it_can_be_turned_off() {
    const std::vector<Entry> entries = {{32, 32, 0, 16, 32 * 32}};
    std::vector<uint8_t> bytes = pack(entries);
    vortex::gamedata::set_asset_corrections(false);
    check(!vortex::gamedata::asset_corrections_enabled(), "the switch reads back off");
    check(vortex::gamedata::correct_asset("tex", bytes) == 0, "nothing is corrected while off");
    check(bytes[entry_at(entries, 0) + BITS_AT] == 16, "the shipped label stands");
    vortex::gamedata::set_asset_corrections(true);
    check(vortex::gamedata::correct_asset("tex", bytes) == 1, "and on again it is corrected");
}

}  // namespace

int main() {
    check(vortex::gamedata::asset_corrections_enabled(), "corrections are on by default");
    test_the_mislabelled_entry_is_corrected();
    test_a_paletted_entry_is_left_alone();
    test_an_entry_matching_neither_label_is_left_alone();
    test_other_files_are_untouched();
    test_a_file_that_only_looks_like_a_pack_is_refused();
    test_it_can_be_turned_off();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("asset_fixes_test: all checks passed");
    return EXIT_SUCCESS;
}
