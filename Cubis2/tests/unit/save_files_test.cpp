// The game's own files, through the calls the game really uses for them.
//
// Cubis 2 keeps two files of its own at the root of its folder (PLAN.md difference 3):
// `cubissave.dat` (21 760 bytes) and `cubisgame.dat` (9 264). Neither is named by the game's own
// `Manifest.plist`, which is how they were identified. What is measured about them is the boot:
// each is opened exactly once, mode 1, **with the whole file's worth of buffer already attached
// and no transfer afterwards** — a load-on-open, not a write (src/libeapp/async_file.cpp). So
// the rules the file layer is held to are these, each the one a title before this paid for
// getting wrong:
//
//   * both names are saves: they live in the platform's store, not the game directory, so a
//     platform that keeps saves somewhere unusual reads them back from there too;
//   * **a save opens to be read whatever mode it names.** Mode 1 is what this game asks with,
//     and a layer that believed it would point the game's buffer at the file and write
//     uninitialised memory over the save on every launch — which is what it cost The Sims
//     Bowling;
//   * a save that has never been written still opens, empty, so the game can decide from what it
//     does not find inside rather than from a refused open;
//   * what the store kept is what comes back, through a transfer or through a load-on-open, and
//     reading it does not consume it;
//   * a store's names are flat, so a save in a subdirectory would be kept under a key of its own
//     rather than landing on top of a root file of the same name. Neither of this title's two is
//     in a subdirectory; the rule is the store's and is checked here because losing it is silent.
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
constexpr uint32_t REQUEST = cubis::RAM_BASE + 0x1000;
constexpr uint32_t PATH = cubis::RAM_BASE + 0x1100;
constexpr uint32_t BUFFER = cubis::RAM_BASE + 0x1200;
constexpr uint32_t FILE_OBJECT = cubis::RAM_BASE + 0x2000;

// The request fields the file layer reads and writes (src/libeapp/async_file.cpp).
constexpr uint32_t REQUEST_OPERATION = 0x04;
constexpr uint32_t REQUEST_FILE_OBJECT = 0x08;
constexpr uint32_t REQUEST_BUFFER = 0x14;
constexpr uint32_t REQUEST_LENGTH = 0x18;
constexpr uint32_t REQUEST_SIZE_RESULT = 0x24;
constexpr uint32_t OPEN_READ = 0;
constexpr uint32_t OPEN_WRITE = 1;
// The operation code this game uses for a transfer, whichever way it goes.
constexpr uint32_t OPERATION_TRANSFER = 3;

const char* const SAVE = "cubissave.dat";
const char* const GAME = "cubisgame.dat";
// Not one of this game's names: a save in a subdirectory, to hold the store's flat-name rule.
const char* const NESTED = "en/cubissave.dat";

void put_string(uint32_t address, const std::string& text) {
    for (size_t i = 0; i < text.size(); ++i) {
        cubis::st8(address + static_cast<uint32_t>(i), static_cast<uint8_t>(text[i]));
    }
    cubis::st8(address + static_cast<uint32_t>(text.size()), 0);
}

void put_bytes(uint32_t address, const std::string& bytes) {
    for (size_t i = 0; i < bytes.size(); ++i) {
        cubis::st8(address + static_cast<uint32_t>(i), static_cast<uint8_t>(bytes[i]));
    }
}

std::string take_bytes(uint32_t address, size_t count) {
    std::string out(count, '\0');
    for (size_t i = 0; i < count; ++i) {
        out[i] = static_cast<char>(cubis::ld8(address + static_cast<uint32_t>(i)));
    }
    return out;
}

// Clear the request and name the file. Each case builds its own; a stale field is exactly the
// kind of thing that would make one case pass on another's leavings.
void begin_request(const std::string& name, uint32_t buffer, uint32_t length) {
    for (uint32_t offset = 0; offset < 0x40; offset += 4) {
        cubis::st32(REQUEST + offset, 0);
    }
    for (uint32_t offset = 0; offset < 0x20; offset += 4) {
        cubis::st32(FILE_OBJECT + offset, 0);
    }
    put_string(PATH, name);
    cubis::st32(REQUEST + REQUEST_FILE_OBJECT, FILE_OBJECT);
    cubis::st32(REQUEST + REQUEST_BUFFER, buffer);
    cubis::st32(REQUEST + REQUEST_LENGTH, length);
    cubis::st8(REQUEST + REQUEST_OPERATION, static_cast<uint8_t>(OPERATION_TRANSFER));
}

// Put `contents` where a save lives. This game never asks the *file* calls to write one — its
// two are only ever opened as loads (async_file.cpp) — so the store is filled directly, which is
// also what makes the mode rule below testable: everything after this reads.
void write_file(const std::string& name, const std::string& contents) {
    const std::vector<uint8_t> bytes(contents.begin(), contents.end());
    check(cubis::platform::save_store().store(cubis::storage::save_key(name), bytes),
          "the store keeps a save");
}

// Read it back: a read-mode open with no buffer, one transfer out, a close.
std::string read_file(const std::string& name, size_t expected) {
    begin_request(name, 0, 0);
    check(cubis::storage::open(OPEN_READ, PATH, 0, REQUEST) != 0, "a read-mode open succeeds");
    begin_request(name, BUFFER, static_cast<uint32_t>(expected));
    put_bytes(BUFFER, std::string(expected, '?'));  // so a read of nothing cannot look like a hit
    const uint32_t read = cubis::storage::perform(REQUEST);
    check(cubis::storage::close(REQUEST) != 0, "and closes again");
    return take_bytes(BUFFER, read);
}

// Read it back as a load-on-open: a read-mode open that carries the buffer.
std::string load_on_open(const std::string& name, size_t capacity) {
    begin_request(name, BUFFER, static_cast<uint32_t>(capacity));
    put_bytes(BUFFER, std::string(capacity, '?'));
    check(cubis::storage::open_for_read(OPEN_READ, PATH, REQUEST) != 0, "the open succeeds");
    const uint32_t arrived = cubis::ld32(REQUEST + REQUEST_SIZE_RESULT);
    check(cubis::storage::close(REQUEST) != 0, "and closes again");
    return take_bytes(BUFFER, arrived);
}

// A first boot has neither save, and the game decides that from what the open does *not* give
// it rather than from a refusal — an open that fails leaves it waiting for a file that will
// never arrive.
void test_a_missing_save_still_opens_empty() {
    begin_request(SAVE, BUFFER, 64);
    put_bytes(BUFFER, std::string(64, '?'));
    check(cubis::storage::open_for_read(OPEN_WRITE, PATH, REQUEST) != 0,
          "no save yet: the open still succeeds");
    check(cubis::ld32(REQUEST + REQUEST_SIZE_RESULT) == 0, "and reports no bytes");
    check(cubis::storage::close(REQUEST) != 0, "and closes");
}

void test_what_was_written_comes_back() {
    const std::string contents = "\x01\x00\x00\x00\xff\x7f\x00\x00\x00\x00\x00\x00";  // 12 bytes
    write_file(SAVE, contents);
    check(read_file(SAVE, contents.size()) == contents, "the save reads back as stored");
    check(load_on_open(SAVE, 64) == contents, "and arrives with a load-on-open too");
    check(read_file(SAVE, contents.size()) == contents, "and reading it did not consume it");
}

// The mode the game names is 1 — "write" to a plain file layer — and the open must still be a
// load. This is the case that would have caught the fault described at the top of the file.
void test_mode_one_still_reads_a_save() {
    write_file(GAME, "a saved game");
    begin_request(GAME, BUFFER, 64);
    put_bytes(BUFFER, std::string(64, '?'));
    check(cubis::storage::open_for_read(OPEN_WRITE, PATH, REQUEST) != 0, "mode 1 opens it");
    const uint32_t arrived = cubis::ld32(REQUEST + REQUEST_SIZE_RESULT);
    check(cubis::storage::close(REQUEST) != 0, "and closes");
    check(take_bytes(BUFFER, arrived) == "a saved game", "and the save arrived, not the buffer");
}

// Storing again replaces; a save is a whole file, never an append.
void test_a_rewrite_replaces_it() {
    write_file(GAME, std::string(228, 'a'));
    write_file(GAME, std::string(228, 'b'));
    check(read_file(GAME, 228) == std::string(228, 'b'), "the second write is what remains");
}

// A save in a subdirectory and one at the root are different files, and a store that keeps flat
// names must not fold them together.
void test_a_nested_save_is_its_own_file() {
    write_file(SAVE, "root");
    write_file(NESTED, "nested");
    check(read_file(SAVE, 4) == "root", "the root save is untouched");
    check(read_file(NESTED, 6) == "nested", "and the nested one is its own");
}

// Both go to the platform's store, so a platform that keeps them somewhere unusual gets them;
// the game directory is not consulted.
void test_the_store_is_where_they_go() {
    write_file(SAVE, "settings");
    std::vector<uint8_t> bytes;
    check(cubis::platform::save_store().load("cubissave.dat", bytes), "the store has the save");
    check(std::string(bytes.begin(), bytes.end()) == "settings", "with the right bytes in it");
    check(cubis::platform::save_store().load("en.cubissave.dat", bytes),
          "and the nested one under a key of its own");
    check(std::string(bytes.begin(), bytes.end()) == "nested", "with its own bytes");
}

}  // namespace

int main() {
    cubis::guest_memory_init();
    cubis::platform::set_save_store(nullptr);  // the in-memory default: nothing reaches the disk

    test_a_missing_save_still_opens_empty();  // first: the only case with nothing saved
    test_what_was_written_comes_back();
    test_mode_one_still_reads_a_save();
    test_a_rewrite_replaces_it();
    test_a_nested_save_is_its_own_file();
    test_the_store_is_where_they_go();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("save_files_test: all checks passed");
    return EXIT_SUCCESS;
}
