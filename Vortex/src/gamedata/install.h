// Finding the game's files and, the first time, installing them from the player's own copy —
// either the game's folder as it sits on an iPod, or a zip of it. Everything is checked against
// the manifest: the files are copyrighted material the player supplies, and a wrong or damaged
// copy would fail deep inside the game rather than here.
//
// Installing copies rather than pointing at what the player chose, on purpose. The game writes
// into its own folder — saves, and this program's settings — and the folder a player picks is
// very often the only copy they have.
#pragma once

#include <string>

namespace vortex::platform {
class Platform;
}

namespace vortex::gamedata {

// True when every manifest file is present under `game_dir` with the right size and CRC-32.
// Extra files (saves, Finder metadata) are ignored. The first problem is described in `why`.
[[nodiscard]] bool verify_installed(const std::string& game_dir, std::string& why);

// Reads a zip that contains the game's folder (at any depth: "12345/..." or the files at the
// top), verifies every manifest file inside it, and only then writes them under `game_dir`.
[[nodiscard]] bool install_from_zip(const std::string& zip_path, const std::string& game_dir,
                                    std::string& why);

// The same from a directory: either the game's folder itself or its parent, so a player who
// picks `12345` and one who picks the folder containing it both get what they meant.
[[nodiscard]] bool install_from_directory(const std::string& source_dir,
                                          const std::string& game_dir, std::string& why);

// The game directory to run from: `<data_dir>/12345` once it verifies. Until it does, asks the
// platform for the game's folder — or, where the browser cannot choose one, a zip of it — and
// asks again if what it was given is not the game. Empty if the player gives up.
std::string locate_game(platform::Platform& platform, const std::string& data_dir);

}  // namespace vortex::gamedata
