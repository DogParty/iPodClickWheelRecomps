// Where the game keeps its things on each operating system: the installed game files, saves,
// settings and, later, mods. One directory, named for the game, in the place the OS reserves
// for application data.
#pragma once

#include <string>

namespace cubis::platform {

// The per-user data directory, created if missing:
//   macOS    ~/Library/Application Support/iPod Cubis 2
//   Windows  %APPDATA%\iPod Cubis 2
//   Linux    $XDG_DATA_HOME/ipod-cubis, else ~/.local/share/ipod-cubis
// CUBIS_DATA_DIR in the environment overrides all three.
std::string data_directory();

}  // namespace cubis::platform
