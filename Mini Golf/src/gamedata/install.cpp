#include "gamedata/install.h"

#include "gamedata/manifest.h"
#include "gamedata/zip.h"
#include "platform/platform.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>
#include <zlib.h>

namespace minigolf::gamedata {

namespace fs = std::filesystem;

namespace {

bool read_file(const fs::path& path, std::vector<uint8_t>& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

bool matches(const ManifestEntry& entry, const std::vector<uint8_t>& data) {
    return data.size() == entry.size &&
           ::crc32(0, data.data(), static_cast<uInt>(data.size())) == entry.crc32;
}

// Does a zip entry name this manifest file? The zip may hold the folder itself or its
// contents, so the entry is accepted at any depth. macOS resource forks live in "__MACOSX/".
bool names(const ZipEntry& entry, const ManifestEntry& wanted) {
    const std::string& name = entry.name;
    const std::string path = wanted.path;
    if (name.rfind("__MACOSX/", 0) == 0 || name.size() < path.size()) {
        return false;
    }
    if (name.compare(name.size() - path.size(), path.size(), path) != 0) {
        return false;
    }
    return name.size() == path.size() || name[name.size() - path.size() - 1] == '/';
}

}  // namespace

bool verify_installed(const std::string& game_dir, std::string& why) {
    std::vector<uint8_t> data;
    for (size_t i = 0; i < GAME_MANIFEST_SIZE; ++i) {
        const ManifestEntry& entry = GAME_MANIFEST[i];
        if (!read_file(fs::path(game_dir) / entry.path, data)) {
            why = std::string("missing ") + entry.path;
            return false;
        }
        if (!matches(entry, data)) {
            why = std::string("damaged ") + entry.path;
            return false;
        }
    }
    return true;
}

bool install_from_zip(const std::string& zip_path, const std::string& game_dir, std::string& why) {
    ZipArchive archive;
    if (!archive.open(zip_path, why)) {
        return false;
    }

    // Check everything before writing anything, so a bad zip leaves no half-installed game.
    std::vector<std::vector<uint8_t>> contents(GAME_MANIFEST_SIZE);
    for (size_t i = 0; i < GAME_MANIFEST_SIZE; ++i) {
        const ManifestEntry& wanted = GAME_MANIFEST[i];
        const ZipEntry* found = nullptr;
        for (const ZipEntry& entry : archive.entries()) {
            if (names(entry, wanted)) {
                found = &entry;
                break;
            }
        }
        if (found == nullptr) {
            why = std::string("the zip has no ") + wanted.path;
            return false;
        }
        if (!archive.read(*found, contents[i], why)) {
            return false;
        }
        if (!matches(wanted, contents[i])) {
            why = std::string("the zip's ") + wanted.path + " is not the file the game shipped";
            return false;
        }
    }

    std::error_code error;
    for (size_t i = 0; i < GAME_MANIFEST_SIZE; ++i) {
        const fs::path target = fs::path(game_dir) / GAME_MANIFEST[i].path;
        fs::create_directories(target.parent_path(), error);
        std::ofstream file(target, std::ios::binary | std::ios::trunc);
        if (!file.write(reinterpret_cast<const char*>(contents[i].data()),
                        static_cast<std::streamsize>(contents[i].size()))) {
            why = "cannot write " + target.string();
            return false;
        }
    }
    return true;
}

std::string locate_game(platform::Platform& platform, const std::string& data_dir) {
    const std::string game_dir = (fs::path(data_dir) / GAME_DIRECTORY_NAME).string();
    std::string why;
    std::string message = "Choose the zip of the game's folder (8888.zip)";
    while (!verify_installed(game_dir, why)) {
        std::fprintf(stderr, "game data: %s — %s\n", game_dir.c_str(), why.c_str());
        std::string zip_path;
        if (!platform.choose_file(message, "zip", zip_path)) {
            return "";
        }
        if (!install_from_zip(zip_path, game_dir, why)) {
            std::fprintf(stderr, "game data: %s: %s\n", zip_path.c_str(), why.c_str());
            message =
                "That zip is not the game (" + why + ") — choose the zip of the game's folder";
        }
    }
    return game_dir;
}

}  // namespace minigolf::gamedata
