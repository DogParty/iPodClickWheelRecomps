#include "platform/paths.h"

#include <cstdlib>
#include <filesystem>

namespace lost::platform {

namespace {

std::string environment(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
}

std::string default_data_directory() {
#if defined(__SWITCH__)
    // The SD card, where homebrew keeps its things. There is no per-user anything on this
    // console: `switch/` is the folder its own menu loads programs from.
    return "sdmc:/switch/lost";
#elif defined(__APPLE__)
    return environment("HOME") + "/Library/Application Support/iPod Lost";
#elif defined(_WIN32)
    return environment("APPDATA") + "\\iPod Lost";
#else
    const std::string xdg = environment("XDG_DATA_HOME");
    return (xdg.empty() ? environment("HOME") + "/.local/share" : xdg) + "/ipod-lost";
#endif
}

}  // namespace

std::string data_directory() {
    std::string directory = environment("LOST_DATA_DIR");
    if (directory.empty()) {
        directory = default_data_directory();
    }
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    return directory;
}

}  // namespace lost::platform
