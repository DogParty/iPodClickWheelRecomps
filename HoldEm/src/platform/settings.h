// The settings a player chooses about the program rather than the game: how fast it runs, and how
// its 320x240 picture is enlarged. They are kept in one place, in one struct, so that a platform's
// settings UI has something to read and write and every platform saves them the same way — in the
// same store as the saved games and the key bindings (platform/save_store.h).
//
// The game itself knows nothing about any of this. Nothing here reaches the recompiled code.
#pragma once

#include <string>

namespace holdem::platform {

// How the 320x240 picture is enlarged to fill a window that is many times its size. The game
// draws into that buffer and nowhere else — the rasteriser is fixed at the iPod's resolution
// (libeapp/gles.h) — so this is a choice about the last step only, not about detail the game
// never drew.
enum class Scaling {
    Sharp,    // prescaled by a whole number, then smoothed to fit: even pixels, clean edges
    Nearest,  // hard pixel blocks, uneven where the window is not a whole multiple
    Smooth,   // plain bilinear: soft, and softer the larger the window
};
constexpr unsigned SCALING_COUNT = 3;

// In the order above, for a settings UI to offer.
[[nodiscard]] const char* scaling_label(Scaling scaling);

// How many raster pixels the game gets for each of its own. See gfx::set_render_scale: the game
// is never told, and a 1:1 sprite blit still enlarges as whole blocks, so what this buys is
// resolved edges wherever the game's geometry is transformed or scaled — and glyphs, once
// `high_resolution_text` is on.
//
// These two are what a settings UI offers and what a saved file is clamped to. The renderer
// clamps again and has the last word — `gfx::set_render_scale` is the only thing that can
// actually refuse a scale — so if the two ever disagree the picture follows the renderer and
// this pair is merely a menu that offers something it will not get.
constexpr unsigned MIN_RENDER_SCALE = 1;
constexpr unsigned MAX_RENDER_SCALE = 8;

struct Settings {
    // Frames a second to pace to; 0 runs as fast as the machine can. The game's own clock
    // advances a sixtieth of a second a frame whatever the pace, so 60 is the speed its timers
    // were written for and 30 is that at half speed, which is the default across every title.
    unsigned frame_rate = 30;
    bool show_frame_rate = true;  // the live rate in the window title
    Scaling scaling = Scaling::Sharp;
    bool pixel_perfect = false;  // enlarge by whole multiples only, and border the rest

    // --- what this renderer can do that the iPod's could not (all default to the iPod's answer)
    unsigned render_scale = MIN_RENDER_SCALE;
    bool high_resolution_text = false;
};

// The one copy every part of the program reads. A platform applies it (Platform::apply_settings)
// and a settings UI changes it; neither keeps a copy of its own.
[[nodiscard]] Settings& settings();

// Read them from the platform's store, and write them back after a change. Both are no-ops on a
// platform whose store keeps nothing. Loading leaves anything the file does not mention — or the
// whole lot, if the file was written by a version that meant something different by it — at
// whatever the platform had already set.
void load_settings();
void save_settings();

// The saved form, exposed for the tests.
[[nodiscard]] std::string settings_to_text(const Settings& values);
void settings_from_text(Settings& values, const std::string& text);

}  // namespace holdem::platform
