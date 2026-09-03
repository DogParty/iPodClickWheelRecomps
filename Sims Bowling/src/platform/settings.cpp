// See settings.h.
#include "platform/settings.h"

#include "platform/save_store.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace bowling::platform {

namespace {

constexpr const char* SETTINGS_NAME = "settings.txt";

// Bumped whenever the meaning of a setting or the default of one changes, so that a file written
// against the old meaning is left alone rather than quietly restoring it. The bindings file
// learned this the hard way; see input_bindings.cpp.
constexpr unsigned SETTINGS_FORMAT = 1;

Settings current;

// What each scaling is called in the file, which is not what it is called on screen: one is a
// name the program must go on reading, the other is a label that can be reworded.
struct ScalingName {
    Scaling scaling;
    const char* key;
    const char* label;
};

constexpr ScalingName NAMES[SCALING_COUNT] = {
    {Scaling::Sharp, "sharp", "Sharp"},
    {Scaling::Nearest, "nearest", "Nearest"},
    {Scaling::Smooth, "smooth", "Smooth"},
};

// The `format` line's number, or 0 for a file that has none.
unsigned format_of(const std::string& text) {
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        std::string name;
        unsigned version = 0;
        if ((fields >> name) && name == "format" && (fields >> version)) {
            return version;
        }
    }
    return 0;
}

}  // namespace

const char* scaling_label(Scaling scaling) {
    for (const ScalingName& name : NAMES) {
        if (name.scaling == scaling) {
            return name.label;
        }
    }
    return NAMES[0].label;
}

Settings& settings() {
    return current;
}

std::string settings_to_text(const Settings& values) {
    std::ostringstream out;
    out << "# Lost settings. fps 0 means as fast as the machine allows.\n";
    out << "# render-scale 1 is the iPod's own 320x240.\n";
    out << "format " << SETTINGS_FORMAT << '\n';
    out << "fps " << values.frame_rate << '\n';
    out << "show-fps " << (values.show_frame_rate ? 1 : 0) << '\n';
    for (const ScalingName& name : NAMES) {
        if (name.scaling == values.scaling) {
            out << "scaling " << name.key << '\n';
        }
    }
    out << "whole-multiples " << (values.pixel_perfect ? 1 : 0) << '\n';
    out << "render-scale " << values.render_scale << '\n';
    out << "hi-res-text " << (values.high_resolution_text ? 1 : 0) << '\n';
    return out.str();
}

void settings_from_text(Settings& values, const std::string& text) {
    if (format_of(text) != SETTINGS_FORMAT) {
        return;  // written by a version that meant something else by these; leave them alone
    }
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        std::string name, value;
        if (!(fields >> name) || name.empty() || name[0] == '#' || !(fields >> value)) {
            continue;  // a comment, a blank line, or something this build does not understand
        }
        if (name == "fps") {
            values.frame_rate = static_cast<unsigned>(std::strtoul(value.c_str(), nullptr, 10));
        } else if (name == "show-fps") {
            values.show_frame_rate = value != "0";
        } else if (name == "whole-multiples") {
            values.pixel_perfect = value != "0";
        } else if (name == "render-scale") {
            // Clamped rather than refused: a file written by a build that offered more scales
            // than this one should give the nearest thing this one can draw, not fall back to
            // the iPod's resolution without saying why.
            values.render_scale =
                std::clamp(static_cast<unsigned>(std::strtoul(value.c_str(), nullptr, 10)),
                           MIN_RENDER_SCALE, MAX_RENDER_SCALE);
        } else if (name == "hi-res-text") {
            values.high_resolution_text = value != "0";
        } else if (name == "scaling") {
            for (const ScalingName& scaling : NAMES) {
                if (value == scaling.key) {
                    values.scaling = scaling.scaling;
                }
            }
        }
    }
}

void load_settings() {
    std::vector<uint8_t> saved;
    if (!save_store().load(SETTINGS_NAME, saved) || saved.empty()) {
        return;  // none saved yet: whatever the platform set stands
    }
    settings_from_text(current, std::string(saved.begin(), saved.end()));
}

void save_settings() {
    const std::string text = settings_to_text(current);
    (void)save_store().store(SETTINGS_NAME, std::vector<uint8_t>(text.begin(), text.end()));
}

}  // namespace bowling::platform
