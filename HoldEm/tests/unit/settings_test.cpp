// The program's own settings (src/platform/settings.{h,cpp}): frame rate, the title readout, how
// many pixels are drawn and how they are enlarged, and the cheats.
//
// These are the portable half of what a platform's settings window edits — the struct, the file
// it is written to, and the rule that a file written against another meaning is not read. The
// window itself cannot be tested here; this pins everything underneath it.
#include "platform/save_store.h"
#include "platform/settings.h"

#include <cstdio>
#include <filesystem>
#include <string>

namespace {

using namespace holdem::platform;

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void test_defaults() {
    const Settings fresh;
    check(fresh.frame_rate == 60, "the game's own timebase is the default rate");
    check(fresh.show_frame_rate, "and the rate is shown until it is turned off");
    check(fresh.scaling == Scaling::Sharp, "sharp scaling is the default");
    check(!fresh.pixel_perfect, "filling the window is the default");
    // The three that would make this program stop being the iPod. Each is off out of the box, so
    // that a run with no settings file — every test, and every first launch — is the iPod's own
    // answer and nothing else.
    check(fresh.render_scale == MIN_RENDER_SCALE, "the iPod's own resolution is the default");
    check(!fresh.high_resolution_text, "and its own text with it");
    check(!fresh.unlock_all_chapters, "no cheat is on by default");
}

void test_text() {
    Settings written;
    written.frame_rate = 0;  // unlocked
    written.show_frame_rate = false;
    written.scaling = Scaling::Smooth;
    written.pixel_perfect = true;
    written.render_scale = 3;
    written.high_resolution_text = true;
    written.unlock_all_chapters = true;

    Settings read;
    settings_from_text(read, settings_to_text(written));
    check(read.frame_rate == 0, "an unlocked rate survives the round trip");
    check(!read.show_frame_rate, "so does a title with no rate in it");
    check(read.scaling == Scaling::Smooth, "so does the scaling");
    check(read.pixel_perfect, "so does whole multiples only");
    check(read.render_scale == 3, "so does the render scale");
    check(read.high_resolution_text, "so does text at the raster's resolution");
    check(read.unlock_all_chapters, "and so does a cheat that was turned on");

    // A scale this build cannot draw is brought to the nearest one it can, rather than being
    // refused: a settings file that has been through a build offering more should give the
    // closest picture this one has, and say nothing about it.
    Settings clamped;
    settings_from_text(clamped, "format 1\nrender-scale 9\n");
    check(clamped.render_scale == MAX_RENDER_SCALE, "too large a scale is clamped, not refused");
    settings_from_text(clamped, "format 1\nrender-scale 0\n");
    check(clamped.render_scale == MIN_RENDER_SCALE, "and so is one below the iPod's own");

    // A setting this build does not know, and one it does, in the same file.
    Settings partial;
    settings_from_text(partial, "format 1\nfps 30\nsomething-else yes\n");
    check(partial.frame_rate == 30, "a known setting loads past one that is not");
    check(partial.scaling == Scaling::Sharp, "and one the file omits keeps its default");

    // A file written against another meaning of these settings is left alone entirely, rather
    // than restoring a default that has since moved.
    Settings other;
    settings_from_text(other, "format 99\nfps 30\n");
    check(other.frame_rate == 60, "a file of another format changes nothing");
    settings_from_text(other, "fps 30\n");
    check(other.frame_rate == 60, "and neither does one with no format at all");
}

void test_persistence() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "holdem-settings-test";
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    set_save_store(make_directory_save_store(directory.string()));

    settings() = Settings{};
    settings().frame_rate = 144;  // as if --fps=144 had been asked for
    load_settings();              // nothing saved yet
    check(settings().frame_rate == 144, "with nothing saved, what the platform set stands");

    settings().scaling = Scaling::Nearest;
    settings().frame_rate = 30;
    settings().render_scale = 2;
    settings().unlock_all_chapters = true;
    save_settings();

    // A fresh start: the platform's defaults first, then the player's settings over them.
    settings() = Settings{};
    check(settings().scaling == Scaling::Sharp, "a fresh start begins at the defaults");
    load_settings();
    check(settings().scaling == Scaling::Nearest, "and the saved settings are read back");
    check(settings().frame_rate == 30, "all of them");
    check(settings().render_scale == 2, "the render scale among them");
    check(settings().unlock_all_chapters, "and a cheat stays on across a restart");

    settings() = Settings{};
    std::filesystem::remove_all(directory, error);
}

}  // namespace

int main() {
    test_defaults();
    test_text();
    test_persistence();
    if (failures != 0) {
        std::fprintf(stderr, "settings: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("settings: ok");
    return 0;
}
