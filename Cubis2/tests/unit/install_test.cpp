// Installing the game's files (src/gamedata/install.{h,cpp}).
//
// The player's own copy of the game is very often the only one they have, so what matters here
// is that a wrong or damaged source is refused *before* anything is written, that a folder the
// game has since written into still verifies, and that a player who points at the folder above
// the game's gets what they meant. None of that needs the real game: the manifest is a table,
// and a fake one exercises every rule in it.
//
// The manifest is generated (`tools/manifest.py`) and this test links its own instead, which is
// why `GAME_MANIFEST` is defined here rather than in the file that normally supplies it. That
// works because the generated one lives in a static library, whose member is only pulled in if
// the symbol is still undefined — and it is exactly the kind of arrangement that fails silently
// if it ever stops working, so `main` checks which of the two it got before testing anything.
#include "gamedata/install.h"
#include "gamedata/manifest.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <zlib.h>

namespace fs = std::filesystem;

namespace cubis::gamedata {

// Two files, one of them in a subdirectory, because the real manifest has both and the
// subdirectory is the part that needs a directory created for it. The sizes and CRCs are of the
// contents `write` below produces.
namespace {
constexpr const char* FIRST_CONTENTS = "the game's first file";
constexpr const char* SECOND_CONTENTS = "and one inside a folder";
}  // namespace

const ManifestEntry GAME_MANIFEST[] = {
    {"l", 21, 0x4b9fd426u},
    {"Executables/Cubis2_1_1_2563292.bin", 23, 0x50536bfdu},
};
const size_t GAME_MANIFEST_SIZE = 2;

}  // namespace cubis::gamedata

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void write(const fs::path& path, const std::string& contents) {
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << contents;
}

// A source directory holding the game as shipped.
fs::path make_source(const fs::path& root) {
    write(root / "l", cubis::gamedata::FIRST_CONTENTS);
    write(root / cubis::gamedata::GAME_IMAGE_PATH, cubis::gamedata::SECOND_CONTENTS);
    return root;
}

void test_installs_from_the_games_folder(const fs::path& scratch) {
    const fs::path source = make_source(scratch / "from-folder" / "99999");
    const std::string target = (scratch / "installed-folder").string();
    std::string why;
    check(cubis::gamedata::install_from_directory(source.string(), target, why),
          "the game's own folder installs");
    check(cubis::gamedata::verify_installed(target, why), "and then verifies");
    check(fs::exists(fs::path(target) / cubis::gamedata::GAME_IMAGE_PATH),
          "the file in the subdirectory arrived");
}

void test_installs_from_the_folder_above(const fs::path& scratch) {
    // A player who picks the folder that *contains* 99999 means the same thing.
    make_source(scratch / "from-parent" / "99999");
    const std::string target = (scratch / "installed-parent").string();
    std::string why;
    check(cubis::gamedata::install_from_directory((scratch / "from-parent").string(), target, why),
          "the folder above the game's installs too");
}

void test_refuses_a_damaged_source_before_writing(const fs::path& scratch) {
    const fs::path source = make_source(scratch / "damaged" / "99999");
    write(source / "l", "not what the game shipped");
    const std::string target = (scratch / "installed-damaged").string();
    std::string why;
    check(!cubis::gamedata::install_from_directory(source.string(), target, why),
          "a damaged source is refused");
    check(why.find("l") != std::string::npos, "and the reason names the file");
    check(!fs::exists(fs::path(target) / cubis::gamedata::GAME_IMAGE_PATH),
          "nothing was written: a refused install leaves no half-installed game");
}

void test_a_played_folder_still_verifies(const fs::path& scratch) {
    const fs::path source = make_source(scratch / "played" / "99999");
    const std::string target = (scratch / "installed-played").string();
    std::string why;
    check(cubis::gamedata::install_from_directory(source.string(), target, why), "installs");
    // What the game and this program write into the game's folder afterwards.
    write(fs::path(target) / "cubissave.dat", "a saved game");
    write(fs::path(target) / "settings.txt", "format 1\n");
    check(cubis::gamedata::verify_installed(target, why),
          "a folder the game has been played in still verifies");
}

void test_an_empty_directory_is_not_the_game(const fs::path& scratch) {
    const fs::path source = scratch / "empty";
    std::error_code error;
    fs::create_directories(source, error);
    std::string why;
    check(!cubis::gamedata::install_from_directory(source.string(),
                                                    (scratch / "installed-empty").string(), why),
          "an empty directory is refused");
}

}  // namespace

int main() {
    // Which manifest did the linker give us? If it reached past this file into the generated
    // one, every case below would be testing the real game's 85 files against two fake ones and
    // failing for a reason that has nothing to do with what is being tested.
    if (cubis::gamedata::GAME_MANIFEST_SIZE != 2) {
        std::fprintf(stderr, "FAIL: linked the generated manifest (%zu entries), not this test's\n",
                     cubis::gamedata::GAME_MANIFEST_SIZE);
        return EXIT_FAILURE;
    }

    // Check the table above really does describe the contents the test writes, so a wrong CRC
    // here fails as itself rather than as every case at once.
    for (const auto& [contents, entry] :
         {std::pair{cubis::gamedata::FIRST_CONTENTS, cubis::gamedata::GAME_MANIFEST[0]},
          std::pair{cubis::gamedata::SECOND_CONTENTS, cubis::gamedata::GAME_MANIFEST[1]}}) {
        const std::string text = contents;
        check(text.size() == entry.size && ::crc32(0, reinterpret_cast<const Bytef*>(text.data()),
                                                   static_cast<uInt>(text.size())) == entry.crc32,
              "the test's own manifest matches the test's own files");
    }

    std::error_code error;
    const fs::path scratch = fs::temp_directory_path() / "cubis-install-test";
    fs::remove_all(scratch, error);

    test_installs_from_the_games_folder(scratch);
    test_installs_from_the_folder_above(scratch);
    test_refuses_a_damaged_source_before_writing(scratch);
    test_a_played_folder_still_verifies(scratch);
    test_an_empty_directory_is_not_the_game(scratch);

    fs::remove_all(scratch, error);
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("install_test: all checks passed");
    return EXIT_SUCCESS;
}
