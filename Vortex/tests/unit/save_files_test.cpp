// The game's own files, through the calls it really uses for them.
//
// Vortex keeps four (PLAN.md difference 3, and the progress log for how this was established with
// `VORTEX_TRACE_FILES=1`): `options` (12 bytes), `stats` (228), `<lang>/stats` (2 128) and its
// saved game `quick<name>` (18 988, and the player chose that name). Every one of them is
// **written through the store ordinals** — `#12` open, `#16` write, `#14` close — when the game is
// left, and **read through the ordinary file calls** at start-up, with an open that names mode 1.
//
// That asymmetry is the whole of this file, because getting it backwards is what a save costs:
//
//   * a save is opened to be READ whatever mode the open names. Taking the direction from the
//     mode pointed start-up's transfer at the file and wrote the game's *uninitialised* buffer
//     over every save on every launch — `stats` came back 228 bytes of zeros and the saved game
//     was gone before the title screen;
//   * a save that does not exist yet still OPENS, empty. Refusing the open left the game on
//     `LOADING…` for ever, waiting for a file it had no way to create;
//   * anything the store holds is a save, whatever it is called, because `quick<name>` cannot be
//     known in advance;
//   * `<lang>/stats` and `stats` are different files. The store keeps flat names, so the
//     per-language one is keyed apart — without that, 2 128 bytes of statistics landed on top of
//     the 228-byte file every time the game was left.
//
// The store is the in-memory one, so nothing here touches the disk.
#include "framework/storage.h"
#include "platform/save_store.h"
#include "runtime/memory.h"

#include <cstdio>
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

// Somewhere in the guest's RAM to build a request, a record and a buffer. The addresses are
// arbitrary; all that matters is that they are inside a mapped region and do not overlap.
constexpr uint32_t REQUEST = vortex::RAM_BASE + 0x1000;
constexpr uint32_t PATH = vortex::RAM_BASE + 0x1100;
constexpr uint32_t BUFFER = vortex::RAM_BASE + 0x1200;
constexpr uint32_t RECORD = vortex::RAM_BASE + 0x1800;
constexpr uint32_t FILE_OBJECT = vortex::RAM_BASE + 0x2000;

// The request fields the file layer reads and writes (src/libeapp/async_file.cpp).
constexpr uint32_t REQUEST_OPERATION = 0x04;
constexpr uint32_t REQUEST_FILE_OBJECT = 0x08;
constexpr uint32_t REQUEST_BUFFER = 0x14;
constexpr uint32_t REQUEST_LENGTH = 0x18;
constexpr uint32_t REQUEST_SIZE_RESULT = 0x24;
// The mode the game names when it opens a save — the one that reads as "write" elsewhere.
constexpr uint32_t OPEN_MODE_ONE = 1;
// The operation code this game uses for a transfer, whichever way it goes.
constexpr uint32_t OPERATION_TRANSFER = 3;
constexpr uint32_t STORE_OK = 0;
constexpr uint32_t STORE_RECORD_HANDLE = 0;

const char* const OPTIONS = "options";
const char* const STATS = "stats";
const char* const ENGLISH_STATS = "en/stats";
const char* const SAVED_GAME = "quicka";  // `quick` + the name the player typed

void put_string(uint32_t address, const std::string& text) {
    for (size_t i = 0; i < text.size(); ++i) {
        vortex::st8(address + static_cast<uint32_t>(i), static_cast<uint8_t>(text[i]));
    }
    vortex::st8(address + static_cast<uint32_t>(text.size()), 0);
}

void put_bytes(uint32_t address, const std::string& bytes) {
    for (size_t i = 0; i < bytes.size(); ++i) {
        vortex::st8(address + static_cast<uint32_t>(i), static_cast<uint8_t>(bytes[i]));
    }
}

std::string take_bytes(uint32_t address, size_t count) {
    std::string out(count, '\0');
    for (size_t i = 0; i < count; ++i) {
        out[i] = static_cast<char>(vortex::ld8(address + static_cast<uint32_t>(i)));
    }
    return out;
}

// Clear the request and name the file. Each case builds its own; a stale field is exactly the
// kind of thing that would make one case pass on another's leavings.
void begin_request(const std::string& name, uint32_t buffer, uint32_t length) {
    for (uint32_t offset = 0; offset < 0x40; offset += 4) {
        vortex::st32(REQUEST + offset, 0);
    }
    for (uint32_t offset = 0; offset < 0x20; offset += 4) {
        vortex::st32(FILE_OBJECT + offset, 0);
    }
    put_string(PATH, name);
    vortex::st32(REQUEST + REQUEST_FILE_OBJECT, FILE_OBJECT);
    vortex::st32(REQUEST + REQUEST_BUFFER, buffer);
    vortex::st32(REQUEST + REQUEST_LENGTH, length);
    vortex::st8(REQUEST + REQUEST_OPERATION, static_cast<uint8_t>(OPERATION_TRANSFER));
}

// Save `contents` the way the game does when it is left: store open, one write, close.
void save(const std::string& name, const std::string& contents) {
    put_string(PATH, name);
    vortex::st32(RECORD + STORE_RECORD_HANDLE, 0);
    check(vortex::storage::store_open(1, PATH, RECORD) == STORE_OK, "the store opens the save");
    const uint32_t handle = vortex::ld32(RECORD + STORE_RECORD_HANDLE);
    check(handle != 0, "and leaves a handle in the record");
    put_bytes(BUFFER, contents);
    check(vortex::storage::store_write(handle, BUFFER, static_cast<uint32_t>(contents.size())) ==
              STORE_OK,
          "the whole save goes over in one call");
    check(vortex::storage::store_close(handle) == STORE_OK, "and closing it commits");
}

// Load it the way the game does at start-up: one mode-1 open, one transfer, close. Answers what
// arrived in the buffer.
std::string load(const std::string& name, size_t capacity) {
    begin_request(name, 0, 0);
    check(vortex::storage::open(OPEN_MODE_ONE, PATH, 0, REQUEST) != 0, "the save opens");
    begin_request(name, BUFFER, static_cast<uint32_t>(capacity));
    put_bytes(BUFFER, std::string(capacity, '?'));  // so a read of nothing cannot look like a hit
    const uint32_t read = vortex::storage::perform(REQUEST);
    check(vortex::storage::close(REQUEST) != 0, "and closes again");
    return take_bytes(BUFFER, read);
}

// The rule that costs a save if it is missed: start-up opens with mode 1, and neither the open
// nor the transfer that follows it may put anything *into* the file.
void test_start_up_reads_a_save_and_does_not_write_it() {
    const std::string contents = "the game as it was left";
    save(SAVED_GAME, contents);
    check(load(SAVED_GAME, contents.size()) == contents, "the saved game loads");
    check(load(SAVED_GAME, contents.size()) == contents, "and loading it did not consume it");

    // The same open with nothing transferred at all, then a load: still there.
    begin_request(SAVED_GAME, 0, 0);
    check(vortex::storage::open(OPEN_MODE_ONE, PATH, 0, REQUEST) != 0, "it opens once more");
    check(vortex::storage::close(REQUEST) != 0, "and closes with nothing transferred");
    check(load(SAVED_GAME, contents.size()) == contents, "an open alone leaves the save alone");

    // And a transfer whose buffer holds something else does not become the file's contents,
    // which is precisely the fault this test exists for.
    begin_request(SAVED_GAME, BUFFER, static_cast<uint32_t>(contents.size()));
    put_bytes(BUFFER, std::string(contents.size(), 'X'));
    static_cast<void>(vortex::storage::perform(REQUEST));
    static_cast<void>(vortex::storage::close(REQUEST));
    std::vector<uint8_t> kept;
    check(vortex::platform::save_store().load(SAVED_GAME, kept), "the store still has it");
    check(std::string(kept.begin(), kept.end()) == contents, "with the bytes that were saved");
}

// A save that has never been written still opens — the game asks before it has ever saved, and
// decides there is none from finding nothing inside.
void test_a_save_that_does_not_exist_opens_empty() {
    begin_request("quickz", 0, 0);
    check(vortex::storage::open(OPEN_MODE_ONE, PATH, 0, REQUEST) != 0,
          "a save that was never written still opens");
    check(vortex::storage::close(REQUEST) != 0, "and closes");
    check(load(OPTIONS, 12).empty(), "and a fixed-name save reads back as nothing at all");
}

// `en/stats` and `stats` are different files, and a store that keeps flat names must not fold
// them together — 2 128 bytes of statistics landed on top of the 228-byte file when it did.
void test_the_language_stats_are_their_own_file() {
    save(STATS, "root");
    save(ENGLISH_STATS, "english");
    check(load(STATS, 4) == "root", "the root stats are untouched");
    check(load(ENGLISH_STATS, 7) == "english", "and the per-language ones are theirs");
}

// The four go to the platform's store, so a platform that keeps them somewhere unusual gets
// them; the game directory is not consulted.
void test_the_store_is_where_they_go() {
    save(OPTIONS, "settings");
    std::vector<uint8_t> bytes;
    check(vortex::platform::save_store().load(OPTIONS, bytes), "the store has options");
    check(std::string(bytes.begin(), bytes.end()) == "settings", "with the right bytes in it");
    check(vortex::platform::save_store().load("en.stats", bytes), "and en/stats under its own key");
    check(std::string(bytes.begin(), bytes.end()) == "english", "with its own bytes");
}

}  // namespace

int main() {
    vortex::guest_memory_init();
    vortex::platform::set_save_store(nullptr);  // the in-memory default: nothing reaches the disk

    test_a_save_that_does_not_exist_opens_empty();  // first: the only case that starts with none
    test_start_up_reads_a_save_and_does_not_write_it();
    test_the_language_stats_are_their_own_file();
    test_the_store_is_where_they_go();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("save_files_test: all checks passed");
    return EXIT_SUCCESS;
}
