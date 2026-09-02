// The save, through the calls the game really uses for it.
//
// The Sims Bowling keeps one save, `savefile.dat` at the root of its folder (PLAN.md difference
// 6). What is measured about it so far is its boot: the game opens it with *write* mode and an
// 18 784-byte buffer — the shape of an open-as-load — and on the device creates it empty. How it
// writes one is not yet established (no recorded case saves), so the rules below are the ones
// the file layer is held to on the evidence there is, and the ones every title before this one
// paid for getting wrong:
//
//   * `savefile.dat` is a save: it lives in the platform's store, not the game directory, so a
//     platform that keeps saves somewhere unusual reads them back from there too.
//   * an open does not truncate, whatever mode names it. Emptying a save on the way to reading
//     it is how one stops surviving a restart.
//   * a transfer on a save reads *out* of it, and an open that carries a buffer loads it. Taking
//     the direction from the open's mode instead pointed start-up's transfer at the file and
//     overwrote the save with the buffer meant to receive it, on another title, with no symptom
//     at all until the player went looking for their game.
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
constexpr uint32_t REQUEST = bowling::RAM_BASE + 0x1000;
constexpr uint32_t PATH = bowling::RAM_BASE + 0x1100;
constexpr uint32_t BUFFER = bowling::RAM_BASE + 0x1200;
constexpr uint32_t RECORD = bowling::RAM_BASE + 0x1800;
constexpr uint32_t FILE_OBJECT = bowling::RAM_BASE + 0x2000;

// The request fields the file layer reads and writes (src/libeapp/async_file.cpp).
constexpr uint32_t REQUEST_OPERATION = 0x04;
constexpr uint32_t REQUEST_FILE_OBJECT = 0x08;
constexpr uint32_t REQUEST_BUFFER = 0x14;
constexpr uint32_t REQUEST_LENGTH = 0x18;
constexpr uint32_t REQUEST_SIZE_RESULT = 0x24;
constexpr uint32_t FILE_OBJECT_RESULT = 0x08;
constexpr uint32_t OPEN_WRITE = 1;
// The operation code this game uses for a transfer, whichever way it goes.
constexpr uint32_t OPERATION_TRANSFER = 3;
// What the store answers when it accepts, and where it leaves the handle.
constexpr uint32_t STORE_OK = 0;
constexpr uint32_t STORE_RECORD_HANDLE = 0;

const char* const SAVE_NAME = "savefile.dat";

void put_string(uint32_t address, const std::string& text) {
    for (size_t i = 0; i < text.size(); ++i) {
        bowling::st8(address + static_cast<uint32_t>(i), static_cast<uint8_t>(text[i]));
    }
    bowling::st8(address + static_cast<uint32_t>(text.size()), 0);
}

void put_bytes(uint32_t address, const std::string& bytes) {
    for (size_t i = 0; i < bytes.size(); ++i) {
        bowling::st8(address + static_cast<uint32_t>(i), static_cast<uint8_t>(bytes[i]));
    }
}

std::string take_bytes(uint32_t address, size_t count) {
    std::string out(count, '\0');
    for (size_t i = 0; i < count; ++i) {
        out[i] = static_cast<char>(bowling::ld8(address + static_cast<uint32_t>(i)));
    }
    return out;
}

// Clear the request and name the file. Each case builds its own; a stale field is exactly the
// kind of thing that would make one case pass on another's leavings.
void begin_request(const std::string& name, uint32_t buffer, uint32_t length) {
    for (uint32_t offset = 0; offset < 0x40; offset += 4) {
        bowling::st32(REQUEST + offset, 0);
    }
    for (uint32_t offset = 0; offset < 0x20; offset += 4) {
        bowling::st32(FILE_OBJECT + offset, 0);
    }
    put_string(PATH, name);
    bowling::st32(REQUEST + REQUEST_FILE_OBJECT, FILE_OBJECT);
    bowling::st32(REQUEST + REQUEST_BUFFER, buffer);
    bowling::st32(REQUEST + REQUEST_LENGTH, length);
    bowling::st8(REQUEST + REQUEST_OPERATION, static_cast<uint8_t>(OPERATION_TRANSFER));
}

// Put `contents` in the store under `name`, through the store calls (#12/#16/#14).
void save(const std::string& name, const std::string& contents) {
    put_string(PATH, name);
    bowling::st32(RECORD + STORE_RECORD_HANDLE, 0);
    check(bowling::storage::store_open(1, PATH, RECORD) == STORE_OK, "the store opens the save");
    const uint32_t handle = bowling::ld32(RECORD + STORE_RECORD_HANDLE);
    check(handle != 0, "and leaves a handle in the record");
    put_bytes(BUFFER, contents);
    check(bowling::storage::store_write(handle, BUFFER, static_cast<uint32_t>(contents.size())) ==
              STORE_OK,
          "the whole save goes over in one call");
    check(bowling::storage::store_close(handle) == STORE_OK, "and closing it commits");
}

// Read it back through the file path: opened with *write* mode, one transfer, closed.
std::string load(const std::string& name, size_t expected) {
    begin_request(name, 0, 0);
    check(bowling::storage::open(OPEN_WRITE, PATH, 0, REQUEST) != 0, "the save opens");
    begin_request(name, BUFFER, static_cast<uint32_t>(expected));
    put_bytes(BUFFER, std::string(expected, '?'));  // so a read of nothing cannot look like a hit
    const uint32_t read = bowling::storage::perform(REQUEST);
    check(bowling::storage::close(REQUEST) != 0, "and closes again");
    return take_bytes(BUFFER, read);
}

// Read it back the way this game's boot does: one open, write mode, with a buffer to load into.
std::string load_on_open(const std::string& name, size_t capacity) {
    begin_request(name, BUFFER, static_cast<uint32_t>(capacity));
    put_bytes(BUFFER, std::string(capacity, '?'));
    check(bowling::storage::open_for_read(OPEN_WRITE, PATH, REQUEST) != 0, "the save opens");
    const uint32_t arrived = bowling::ld32(REQUEST + REQUEST_SIZE_RESULT);
    check(bowling::ld32(FILE_OBJECT + FILE_OBJECT_RESULT) == arrived,
          "the file object's result is the byte count too");
    check(bowling::storage::close(REQUEST) != 0, "and closes again");
    return take_bytes(BUFFER, arrived);
}

void test_a_saved_game_survives() {
    const std::string contents = "a saved game, or near enough for a test";
    save(SAVE_NAME, contents);
    check(load(SAVE_NAME, contents.size()) == contents, "what was saved is what comes back");
}

// The rule that costs a save if it is missed: start-up opens with write mode, and neither the
// open nor the transfer that follows it may put anything *into* the file.
void test_loading_does_not_empty_it() {
    const std::string contents = "the game before it was opened again";
    save(SAVE_NAME, contents);
    check(load(SAVE_NAME, contents.size()) == contents, "it loads once");
    check(load(SAVE_NAME, contents.size()) == contents, "and loading it did not consume it");

    // The same open with nothing transferred at all.
    begin_request(SAVE_NAME, 0, 0);
    check(bowling::storage::open(OPEN_WRITE, PATH, 0, REQUEST) != 0, "it opens once more");
    check(bowling::storage::close(REQUEST) != 0, "and closes with nothing transferred");
    check(load(SAVE_NAME, contents.size()) == contents,
          "an open with nothing transferred leaves the save alone");
}

// This game's own boot: `savefile.dat` opened with write mode and a buffer larger than any
// save. Nothing saved yet reads as nothing arriving; a save reads as the save.
void test_the_boot_open_loads_the_save() {
    check(load_on_open(SAVE_NAME, 64).empty(), "no save yet: nothing arrives");
    const std::string contents = "a save the boot finds";
    save(SAVE_NAME, contents);
    check(load_on_open(SAVE_NAME, 64) == contents, "a saved game arrives with the open");
    check(load_on_open(SAVE_NAME, 64) == contents, "and the open did not consume it");
}

// A save goes to the platform's store, so a platform that keeps them somewhere unusual gets
// them; the game directory is not consulted.
void test_the_store_is_where_it_goes() {
    save(SAVE_NAME, "settings");
    std::vector<uint8_t> bytes;
    check(bowling::platform::save_store().load(SAVE_NAME, bytes), "the store has it");
    check(std::string(bytes.begin(), bytes.end()) == "settings", "with the right bytes in it");
}

}  // namespace

int main() {
    bowling::guest_memory_init();
    bowling::platform::set_save_store(nullptr);  // the in-memory default: nothing reaches the disk

    test_the_boot_open_loads_the_save();  // first: the only case that starts with no save
    test_a_saved_game_survives();
    test_loading_does_not_empty_it();
    test_the_store_is_where_it_goes();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("save_files_test: all checks passed");
    return EXIT_SUCCESS;
}
