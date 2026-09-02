// Where the game keeps its things on each operating system: the installed game files, saves,
// settings and, later, mods. One directory, named for the game, in the place the OS reserves
// for application data.
#pragma once

#include <string>

namespace holdem::platform {

// The per-user data directory, created if missing:
//   macOS    ~/Library/Application Support/iPod Texas Hold'em
//   Windows  %APPDATA%\iPod Texas Hold'em
//   Linux    $XDG_DATA_HOME/ipod-holdem, else ~/.local/share/ipod-holdem
// HOLDEM_DATA_DIR in the environment overrides all three.
std::string data_directory();

}  // namespace holdem::platform
