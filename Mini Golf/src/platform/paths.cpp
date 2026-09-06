#include "platform/paths.h"

#include <cstdlib>
#include <filesystem>

namespace minigolf::platform {

namespace {

std::string environment(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
}

// What `set_data_directory` was told, if it was told anything.
std::string& platform_directory() {
    static std::string directory;
    return directory;
}

std::string default_data_directory() {
#if defined(__SWITCH__)
    // The SD card, where homebrew keeps its things. There is no per-user anything on this
    // console: `switch/` is the folder its own menu loads programs from.
    return "sdmc:/switch/minigolf";
#elif defined(__APPLE__)
    return environment("HOME") + "/Library/Application Support/iPod Mini Golf";
#elif defined(_WIN32)
    return environment("APPDATA") + "\\iPod Mini Golf";
#elif defined(__ANDROID__)
    // Never reached by the app itself: its platform calls `set_data_directory` with the storage
    // the system gave it, which no path written here could have guessed. This is what is left
    // for a plain command-line build — the on-device test tool — which is run from the
    // directory holding the game's folder, or told another with MINIGOLF_DATA_DIR.
    return ".";
#else
    const std::string xdg = environment("XDG_DATA_HOME");
    return (xdg.empty() ? environment("HOME") + "/.local/share" : xdg) + "/ipod-mini-golf";
#endif
}

}  // namespace

void set_data_directory(const std::string& directory) {
    platform_directory() = directory;
}

std::string data_directory() {
    // The environment first, so a run can always be pointed somewhere; then whatever the
    // platform said; then the place this operating system keeps application data.
    std::string directory = environment("MINIGOLF_DATA_DIR");
    if (directory.empty()) {
        directory = platform_directory();
    }
    if (directory.empty()) {
        directory = default_data_directory();
    }
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    return directory;
}

}  // namespace minigolf::platform
