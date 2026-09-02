// Finding the game's files and, the first time, installing them from a zip of the game's
// folder. Everything is checked against the manifest: the files are copyrighted material the
// player supplies, and a wrong or damaged copy would fail deep inside the game rather than here.
#pragma once

#include <string>

namespace minigolf::platform {
class Platform;
}

namespace minigolf::gamedata {

// True when every manifest file is present under `game_dir` with the right size and CRC-32.
// Extra files (saves, Finder metadata) are ignored. The first problem is described in `why`.
[[nodiscard]] bool verify_installed(const std::string& game_dir, std::string& why);

// Reads a zip that contains the game's folder (at any depth: "88888/..." or the files at the
// top), verifies every manifest file inside it, and only then writes them under `game_dir`.
[[nodiscard]] bool install_from_zip(const std::string& zip_path, const std::string& game_dir,
                                    std::string& why);

// The game directory to run from: `<data_dir>/88888` once it verifies. Until it does, asks the
// platform for a zip to install from, repeating on a bad zip; empty if the player gives up.
std::string locate_game(platform::Platform& platform, const std::string& data_dir);

}  // namespace minigolf::gamedata
