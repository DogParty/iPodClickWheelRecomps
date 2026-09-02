// Where the game keeps its things on each operating system: the installed game files, saves,
// settings and, later, mods. One directory, named for the game, in the place the OS reserves
// for application data.
#pragma once

#include <string>

namespace vortex::platform {

// The per-user data directory, created if missing:
//   macOS    ~/Library/Application Support/iPod Vortex
//   Windows  %APPDATA%\iPod Vortex
//   Linux    $XDG_DATA_HOME/ipod-vortex, else ~/.local/share/ipod-vortex
// VORTEX_DATA_DIR in the environment overrides all three.
std::string data_directory();

}  // namespace vortex::platform
