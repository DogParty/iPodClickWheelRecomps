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

// Say where that directory is, for a platform whose answer this file cannot work out on its
// own. Android is the case: an app may write only inside storage the system hands it, and only
// the app can ask for it, so the platform passes it here from its constructor — before anything
// reads it (runtime/main.cpp creates the platform first). The environment still wins, so a
// build driven from a command line can still be pointed somewhere else.
void set_data_directory(const std::string& directory);

}  // namespace minigolf::platform
