// Where the game keeps its things on each operating system: the installed game files, saves,
// settings and, later, mods. One directory, named for the game, in the place the OS reserves
// for application data.
#pragma once

#include <string>

namespace minigolf::platform {

// The per-user data directory, created if missing:
//   macOS    ~/Library/Application Support/iPod Mini Golf
//   Windows  %APPDATA%\iPod Mini Golf
//   Linux    $XDG_DATA_HOME/ipod-mini-golf, else ~/.local/share/ipod-mini-golf
// MINIGOLF_DATA_DIR in the environment overrides all three.
std::string data_directory();

}  // namespace minigolf::platform
