// Saving and loading a game, through the two different calls the game really uses for them.
//
// The shape of this is the whole point, and it was got wrong twice before it was measured. A
// saved game does not go out the way it comes in:
//
//   * **Writing** a save is `AsyncFileIO #12/#16/#14` — the store: open by name, hand over the
//     whole thing, close. Save and Exit was watched doing it three times in one frame, for
//     `options.sav` (26 bytes), `lost.sav0` (12 524) and one more of 88, with no transfer to any
//     file anywhere in the run.
//   * **Reading** one back is the ordinary file path — `#0` open, `#3` transfer, `#1` close — at
//     start-up, and the game opens it with *write* mode to do it.
//
// So the mode a save is opened with says nothing about which way the bytes will move, and the
// three rules below are the three ways that has been got wrong:
//
//   * a name like `lost.sav0` is a save. The suffix carries the slot number, so a plain match on
//     `.sav` sees `options.sav` and misses every actual saved game.
//   * an open does not truncate. Emptying a save on the way to reading it is how one stops
//     surviving a restart.
//   * a transfer on a save reads *out* of it. Taking the direction from the open's mode instead
//     pointed start-up's transfer at the file and overwrote the save with the buffer that was
//     meant to receive it — 12 333 of 12 524 bytes gone on the first launch after saving, which
//     is a fault with no symptom at all until the player goes looking for their game.
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
constexpr uint32_t REQUEST = lost::RAM_BASE + 0x1000;
constexpr uint32_t PATH = lost::RAM_BASE + 0x1100;
constexpr uint32_t BUFFER = lost::RAM_BASE + 0x1200;
constexpr uint32_t RECORD = lost::RAM_BASE + 0x1800;

// The request fields the file layer reads (src/libeapp/async_file.cpp).
constexpr uint32_t REQUEST_OPERATION = 0x04;
constexpr uint32_t REQUEST_FILE_OBJECT = 0x08;
constexpr uint32_t REQUEST_BUFFER = 0x14;
constexpr uint32_t REQUEST_LENGTH = 0x18;
constexpr uint32_t OPEN_WRITE = 1;
// The one operation code this game uses for every transfer, whichever way it goes.
constexpr uint32_t OPERATION_TRANSFER = 3;
// What the store answers when it accepts, and where it leaves the handle.
constexpr uint32_t STORE_OK = 0;
constexpr uint32_t STORE_RECORD_HANDLE = 0;

void put_string(uint32_t address, const std::string& text) {
    for (size_t i = 0; i < text.size(); ++i) {
        lost::st8(address + static_cast<uint32_t>(i), static_cast<uint8_t>(text[i]));
    }
    lost::st8(address + static_cast<uint32_t>(text.size()), 0);
}

void put_bytes(uint32_t address, const std::string& bytes) {
    for (size_t i = 0; i < bytes.size(); ++i) {
        lost::st8(address + static_cast<uint32_t>(i), static_cast<uint8_t>(bytes[i]));
    }
}

std::string take_bytes(uint32_t address, size_t count) {
    std::string out(count, '\0');
    for (size_t i = 0; i < count; ++i) {
        out[i] = static_cast<char>(lost::ld8(address + static_cast<uint32_t>(i)));
    }
    return out;
}

// Clear the request and name the file. Each case builds its own; a stale field is exactly the
// kind of thing that would make one case pass on another's leavings.
void begin_request(const std::string& name, uint32_t buffer, uint32_t length) {
    for (uint32_t offset = 0; offset < 0x40; offset += 4) {
        lost::st32(REQUEST + offset, 0);
    }
    put_string(PATH, name);
    lost::st32(REQUEST + REQUEST_FILE_OBJECT, lost::RAM_BASE + 0x2000);
    lost::st32(REQUEST + REQUEST_BUFFER, buffer);
    lost::st32(REQUEST + REQUEST_LENGTH, length);
    lost::st8(REQUEST + REQUEST_OPERATION, static_cast<uint8_t>(OPERATION_TRANSFER));
}

// Save `contents` under `name` the way Save and Exit does: through the store.
void save(const std::string& name, const std::string& contents) {
    put_string(PATH, name);
    lost::st32(RECORD + STORE_RECORD_HANDLE, 0);
    check(lost::storage::store_open(1, PATH, RECORD) == STORE_OK, "the store opens the save");
    const uint32_t handle = lost::ld32(RECORD + STORE_RECORD_HANDLE);
    check(handle != 0, "and leaves a handle in the record");
    put_bytes(BUFFER, contents);
    check(lost::storage::store_write(handle, BUFFER, static_cast<uint32_t>(contents.size())) ==
              STORE_OK,
          "the whole save goes over in one call");
    check(lost::storage::store_close(handle) == STORE_OK, "and closing it commits");
}

// Read it back the way start-up does: opened with *write* mode, one transfer, closed.
std::string load(const std::string& name, size_t expected) {
    begin_request(name, 0, 0);
    check(lost::storage::open(OPEN_WRITE, PATH, 0, REQUEST) != 0, "the save opens");
    begin_request(name, BUFFER, static_cast<uint32_t>(expected));
    put_bytes(BUFFER, std::string(expected, '?'));  // so a read of nothing cannot look like a hit
    const uint32_t read = lost::storage::perform(REQUEST);
    check(lost::storage::close(REQUEST) != 0, "and closes again");
    return take_bytes(BUFFER, read);
}

void test_a_saved_game_survives(const char* name) {
    const std::string contents = "a saved game, or near enough for a test";
    save(name, contents);
    check(load(name, contents.size()) == contents, "what was saved is what comes back");
}

// The rule that costs a save if it is missed: start-up opens with write mode, and neither the
// open nor the transfer that follows it may put anything *into* the file.
void test_loading_does_not_empty_it() {
    const std::string contents = "the game before it was opened again";
    save("lost.sav1", contents);
    check(load("lost.sav1", contents.size()) == contents, "it loads once");
    check(load("lost.sav1", contents.size()) == contents, "and loading it did not consume it");

    // The same open with nothing transferred at all, which is what a run that opens every save
    // at start-up and never touches one of them does.
    begin_request("lost.sav1", 0, 0);
    check(lost::storage::open(OPEN_WRITE, PATH, 0, REQUEST) != 0, "it opens once more");
    check(lost::storage::close(REQUEST) != 0, "and closes with nothing transferred");
    check(load("lost.sav1", contents.size()) == contents,
          "an open with nothing transferred leaves the save alone");
}

// A save goes to the platform's store, so a platform that keeps them somewhere unusual gets
// them; the game directory is not consulted.
void test_the_store_is_where_it_goes() {
    save("options.sav", "settings");
    std::vector<uint8_t> bytes;
    check(lost::platform::save_store().load("options.sav", bytes), "the store has it");
    check(std::string(bytes.begin(), bytes.end()) == "settings", "with the right bytes in it");
}

}  // namespace

int main() {
    lost::guest_memory_init();
    lost::platform::set_save_store(nullptr);  // the in-memory default: nothing reaches the disk

    // Both spellings of a save, because the slot number lives in the suffix and a plain `.sav`
    // match sees only the first of these.
    test_a_saved_game_survives("options.sav");
    test_a_saved_game_survives("lost.sav0");
    test_loading_does_not_empty_it();
    test_the_store_is_where_it_goes();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("save_files_test: all checks passed");
    return EXIT_SUCCESS;
}
