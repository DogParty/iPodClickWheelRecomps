// The save store (src/platform/save_store.{h,cpp}): where a saved game goes.
//
// The game hands over a name and some bytes and expects to read exactly those back, whatever the
// platform does with them in between. These tests pin that contract against both implementations
// the project ships — the in-memory default and the directory-backed one — because a platform
// port's own store has to honour the same one.
#include "platform/save_store.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using vortex::platform::make_directory_save_store;
using vortex::platform::save_store;
using vortex::platform::SaveStore;
using vortex::platform::set_save_store;

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

std::vector<uint8_t> bytes(std::initializer_list<uint8_t> values) {
    return std::vector<uint8_t>(values);
}

// Everything a store must do, whichever one it is.
void test_contract(SaveStore& store, const char* which) {
    std::vector<uint8_t> read_back;
    check(!store.load("jdmgp.sav", read_back), "a save that was never written is absent");

    const std::vector<uint8_t> save = bytes({0xbe, 0xba, 0xde, 0xc0, 1, 2, 3});
    check(store.store("jdmgp.sav", save), "a save can be stored");
    check(store.load("jdmgp.sav", read_back), "and read back");
    check(read_back == save, "byte for byte");

    // A second save replaces the first rather than appending to it.
    const std::vector<uint8_t> shorter = bytes({9, 9});
    check(store.store("jdmgp.sav", shorter), "a save can be replaced");
    check(store.load("jdmgp.sav", read_back) && read_back == shorter,
          "replacing truncates, it does not append");

    // Saves are separate from one another: the game keeps a backup beside the save.
    check(store.store("jdmgp2.sav", save), "a second save can be stored");
    check(store.load("jdmgp.sav", read_back) && read_back == shorter,
          "storing one save leaves the other alone");

    // The statistics arrive as a whole device path; only the name identifies the save, so no
    // store has to reproduce the iPod's directory layout.
    check(store.store("/Volumes/iPod/Games/stats.dat", save), "a save named by a path stores");
    check(store.load("stats.dat", read_back) && read_back == save,
          "and is found under its plain name");

    // An empty save is a save: reading it back must not look like "there is none".
    check(store.store("empty.sav", {}), "an empty save can be stored");
    check(store.load("empty.sav", read_back) && read_back.empty(), "an empty save reads back");
    std::printf("  %s: contract ok\n", which);
}

}  // namespace

int main() {
    // The default store: no platform has installed one, so nothing reaches the disk.
    test_contract(save_store(), "in-memory default");

    // The directory store, which is what every desktop platform here gets.
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "vortex-save-store-test";
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    set_save_store(make_directory_save_store(directory.string()));
    test_contract(save_store(), "directory");

    // What the directory store wrote really is a file, readable by anything else.
    check(std::filesystem::exists(directory / "jdmgp.sav", error),
          "the directory store writes a file named as the game named the save");
    check(!std::filesystem::exists(directory / "Volumes", error),
          "a save named by a path does not recreate the path");

    // A store installed later replaces the one before it, and does not inherit its saves.
    set_save_store(make_directory_save_store((directory / "second").string()));
    std::vector<uint8_t> read_back;
    check(!save_store().load("jdmgp.sav", read_back), "a fresh store starts empty");

    std::filesystem::remove_all(directory, error);
    if (failures != 0) {
        std::fprintf(stderr, "save store: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("save store: ok");
    return 0;
}
