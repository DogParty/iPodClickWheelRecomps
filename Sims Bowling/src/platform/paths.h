// Where the game keeps its things on each operating system: the installed game files, saves,
// settings and, later, mods. One directory, named for the game, in the place the OS reserves
// for application data.
#pragma once

#include <string>

namespace bowling::platform {

// The per-user data directory, created if missing:
//   macOS    ~/Library/Application Support/iPod The Sims Bowling
//   Windows  %APPDATA%\iPod The Sims Bowling
//   Linux    $XDG_DATA_HOME/ipod-bowling, else ~/.local/share/ipod-bowling
// BOWLING_DATA_DIR in the environment overrides all three.
std::string data_directory();

}  // namespace bowling::platform
