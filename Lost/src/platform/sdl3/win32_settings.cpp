// Win32 side of settings_window.h.
//
// The same four tabs as the Cocoa window, made of the ordinary controls — a tab
// control, static text, drop-down lists, check boxes, one button — in a window of our own that the
// game's window owns, so it stays in front of the game and goes away with it. It is not a dialog
// template: the controls are created by hand and laid out from the same numbers the Cocoa window
// uses, scaled to the monitor's DPI, which is less to get wrong than a resource script and far
// easier to keep in step with the other window.
//
// The window lives on the main thread with the game's. SDL pumps every message the thread gets,
// including this window's, so nothing here needs a loop of its own; what SDL's pump does not do
// is the dialog keyboard handling — Tab between controls, Escape to close — which comes from
// IsDialogMessage, and SDL offers a hook on its pump for exactly that.
#include "platform/sdl3/settings_window.h"

#if defined(_WIN32)

#include "platform/input_bindings.h"
#include "platform/settings.h"

#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE  // the resource macros (IDC_ARROW) in the wide form the calls here take
#endif
// The Win32 headers in the order they need, which is not alphabetical: windows.h first, then
// the two that build on it.
// clang-format off
#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
// clang-format on

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace lost::platform {

namespace {

// The window's size and the layout inside it, in the Cocoa window's units (points at 96 DPI).
// They are the Cocoa window's numbers, so the two look alike.
constexpr int WINDOW_WIDTH = 540;
// Tall enough for the Input tab's row per action, and for the Graphics tab either way.
constexpr int WINDOW_HEIGHT = std::max(480, 150 + 30 * static_cast<int>(ACTION_COUNT));
constexpr int MARGIN = 12;
constexpr int CONTROL_HEIGHT = 24, LABEL_HEIGHT = 20, CHECKBOX_HEIGHT = 20;
constexpr int DROP_LIST_HEIGHT = 200;  // how far a drop-down opens

// Control identifiers. The key menus are a block of their own: one id per action and slot.
constexpr int ID_TABS = 100, ID_FRAME_RATE = 101, ID_SHOW_RATE = 102, ID_RENDER_SCALE = 103,
              ID_SCALING = 104, ID_PIXEL_PERFECT = 105, ID_DEFAULTS = 106, ID_HI_RES_TEXT = 107,
              ID_UNLOCK_CHAPTERS = 108;
constexpr int ID_KEY_BASE = 200;

constexpr unsigned TAB_COUNT = 4;
constexpr unsigned TAB_GENERAL = 0, TAB_INPUT = 1, TAB_GRAPHICS = 2, TAB_CHEATS = 3;

// The tab control's body, as its theme names the part; vsstyle.h has it, under a header some
// toolchains do not ship.
constexpr int THEME_TAB_BODY = 10;

const wchar_t* const WINDOW_CLASS = L"MinigolfSettings";

// UTF-8, as every string in the program is, to what the wide Win32 calls take.
std::wstring wide(const char* text) {
    if (text == nullptr || *text == '\0') {
        return std::wstring();
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (needed <= 0) {
        return std::wstring();
    }
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, &out[0], needed);
    out.resize(static_cast<size_t>(needed) - 1);  // the terminator the count included
    return out;
}

// What the frame-rate list offers, in the order the items appear: 30, 60, and 0 for unlocked.
// A rate the game was started with (--fps=) that is none of those joins the front rather than
// being quietly rounded to one that is.
unsigned rate_choices[4] = {30, 60, 0, 0};
unsigned rate_choice_count = 3;

class SettingsWindow {
public:
    void install(const SettingsHooks& hooks) {
        hooks_ = hooks;
        SDL_SetWindowsMessageHook(message_hook, this);
    }

    void open() {
        if (window_ == nullptr && !create()) {
            return;
        }
        refresh();
        ShowWindow(window_, SW_SHOW);
        SetForegroundWindow(window_);
    }

    void set_frame_rate(unsigned frames_per_second) {
        hooks_.frame_rate = frames_per_second;
        if (window_ != nullptr) {
            show_frame_rate(frames_per_second);
        }
    }

private:
    // --- creation -------------------------------------------------------------------------

    [[nodiscard]] HWND owner() const {
        if (hooks_.game_window == nullptr) {
            return nullptr;
        }
        return static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(hooks_.game_window),
                                                        SDL_PROP_WINDOW_WIN32_HWND_POINTER,
                                                        nullptr));
    }

    // A Cocoa-window measurement at this monitor's DPI.
    [[nodiscard]] int scaled(int points) const {
        return MulDiv(points, static_cast<int>(dpi_), 96);
    }

    bool create() {
        INITCOMMONCONTROLSEX controls{};
        controls.dwSize = sizeof controls;
        controls.dwICC = ICC_TAB_CLASSES | ICC_STANDARD_CLASSES;
        InitCommonControlsEx(&controls);

        static bool registered = false;
        if (!registered) {
            WNDCLASSEXW window_class{};
            window_class.cbSize = sizeof window_class;
            window_class.lpfnWndProc = window_procedure;
            window_class.hInstance = GetModuleHandleW(nullptr);
            window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            window_class.hbrBackground = nullptr;  // painted by hand, see WM_ERASEBKGND
            window_class.lpszClassName = WINDOW_CLASS;
            if (RegisterClassExW(&window_class) == 0) {
                std::fprintf(stderr, "settings: cannot register the window class\n");
                return false;
            }
            registered = true;
        }

        const HWND parent = owner();
        dpi_ = parent != nullptr ? GetDpiForWindow(parent) : GetDpiForSystem();
        if (dpi_ == 0) {
            dpi_ = 96;
        }
        const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
        RECT frame = {0, 0, scaled(WINDOW_WIDTH), scaled(WINDOW_HEIGHT)};
        AdjustWindowRectExForDpi(&frame, style, FALSE, 0, dpi_);
        // Over the middle of the game's window, or of the screen if there is none.
        RECT over = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
        if (parent != nullptr) {
            GetWindowRect(parent, &over);
        }
        const int width = frame.right - frame.left, height = frame.bottom - frame.top;
        const int x = over.left + ((over.right - over.left) - width) / 2;
        const int y = over.top + ((over.bottom - over.top) - height) / 2;
        window_ = CreateWindowExW(0, WINDOW_CLASS, L"Settings", style, x, y, width, height, parent,
                                  nullptr, GetModuleHandleW(nullptr), this);
        if (window_ == nullptr) {
            std::fprintf(stderr, "settings: cannot create the window\n");
            return false;
        }
        make_font();
        theme_ = OpenThemeData(window_, L"Tab");
        build_controls();
        return true;
    }

    // The system's message font at this DPI: what every dialog uses.
    void make_font() {
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof metrics;
        if (SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof metrics, &metrics, 0,
                                       dpi_)) {
            font_ = CreateFontIndirectW(&metrics.lfMessageFont);
        }
        if (font_ == nullptr) {
            font_ = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        }
    }

    // One control, with the window's font and the tab it belongs to. `left` and `top` are
    // within the tab's page, in the Cocoa window's units.
    HWND add(const wchar_t* control_class, const wchar_t* text, DWORD style, int left, int top,
             int width, int height, int id, unsigned tab) {
        HWND control = CreateWindowExW(
            0, control_class, text, style | WS_CHILD | WS_CLIPSIBLINGS, scaled(pane_left_ + left),
            scaled(pane_top_ + top), scaled(width), scaled(height), window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        if (control != nullptr) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
            tab_of_.push_back({control, tab});
        }
        return control;
    }

    HWND label(const char* text, int left, int top, int width, int height, unsigned tab,
               bool secondary) {
        HWND control = add(L"STATIC", wide(text).c_str(), SS_LEFT | SS_NOPREFIX, left, top, width,
                           height, 0, tab);
        if (secondary && control != nullptr) {
            secondary_.push_back(control);
        }
        return control;
    }

    HWND drop_list(int left, int top, int width, int id, unsigned tab) {
        return add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, left, top, width,
                   DROP_LIST_HEIGHT, id, tab);
    }

    HWND check_box(const char* text, int left, int top, int width, int id, unsigned tab) {
        return add(L"BUTTON", wide(text).c_str(), BS_AUTOCHECKBOX | WS_TABSTOP, left, top, width,
                   CHECKBOX_HEIGHT, id, tab);
    }

    static void add_item(HWND list, const std::wstring& text) {
        SendMessageW(list, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }

    void build_controls() {
        // The tab control first; it goes to the bottom of the pile once everything else has
        // been made, since each new child arrives at the bottom and would otherwise be under it.
        tabs_ = CreateWindowExW(0, WC_TABCONTROLW, L"",
                                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP,
                                scaled(MARGIN), scaled(MARGIN), scaled(WINDOW_WIDTH - 2 * MARGIN),
                                scaled(WINDOW_HEIGHT - 2 * MARGIN), window_,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TABS)),
                                GetModuleHandleW(nullptr), nullptr);
        SendMessageW(tabs_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        const wchar_t* titles[TAB_COUNT] = {L"General", L"Input", L"Graphics", L"Cheats"};
        for (unsigned i = 0; i < TAB_COUNT; ++i) {
            std::wstring title = titles[i];
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            item.pszText = &title[0];
            TabCtrl_InsertItem(tabs_, static_cast<int>(i), &item);
        }

        // The tab control decides how much room a tab actually has; ask it rather than guess.
        RECT display = {scaled(MARGIN), scaled(MARGIN), scaled(WINDOW_WIDTH - MARGIN),
                        scaled(WINDOW_HEIGHT - MARGIN)};
        TabCtrl_AdjustRect(tabs_, FALSE, &display);
        display_ = display;
        // Back to the Cocoa window's units for the layout, which happens in those.
        pane_left_ = MulDiv(display.left, 96, static_cast<int>(dpi_));
        pane_top_ = MulDiv(display.top, 96, static_cast<int>(dpi_));
        const int pane_width = MulDiv(display.right - display.left, 96, static_cast<int>(dpi_));

        build_general(pane_width);
        build_input(pane_width);
        build_graphics(pane_width);
        build_cheats(pane_width);
        SetWindowPos(tabs_, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        show_tab(TAB_GENERAL);
    }

    void build_general(int pane_width) {
        const int control_left = 200;
        const int control_width = pane_width - control_left - 16;
        const int y = 20;
        label("FPS", 16, y + 3, control_left - 26, LABEL_HEIGHT, TAB_GENERAL, false);
        frame_rate_ = drop_list(control_left, y, control_width, ID_FRAME_RATE, TAB_GENERAL);
        label("Frames are paced to this rate. Unlocked, the game runs as fast as the machine "
              "allows, which fast-forwards it.",
              16, y + 40, pane_width - 32, 34, TAB_GENERAL, true);
        show_rate_ = check_box("Show FPS in the title bar", control_left, y + 84, control_width,
                               ID_SHOW_RATE, TAB_GENERAL);
    }

    void build_input(int pane_width) {
        const int row_height = 30;
        const int menu_left = 150;
        const int menu_width = (pane_width - menu_left - 16 - 10) / static_cast<int>(BINDING_SLOTS);
        int y = 12;
        label("Choose the keys for each control. Either one does it; changes apply straight "
              "away.",
              16, y, pane_width - 32, 18, TAB_INPUT, true);
        y += row_height;

        unsigned choice_count = 0;
        const InputChoice* choices = assignable_inputs(choice_count);
        const Action* order = all_actions();
        for (unsigned i = 0; i < ACTION_COUNT; ++i) {
            label(action_label(order[i]), 16, y + 3, menu_left - 26, LABEL_HEIGHT, TAB_INPUT,
                  false);
            // Item 0 is "nothing bound"; item c + 1 is choices[c]. Its position in the list is
            // the only thing that maps an item to a key, so there is nothing to get out of step.
            for (unsigned slot = 0; slot < BINDING_SLOTS; ++slot) {
                const int left = menu_left + static_cast<int>(slot) * (menu_width + 10);
                HWND menu = drop_list(left, y, menu_width, key_id(i, slot), TAB_INPUT);
                add_item(menu, L"—");
                for (unsigned c = 0; c < choice_count; ++c) {
                    add_item(menu, wide(choices[c].label));
                }
                key_menus_[i][slot] = menu;
            }
            y += row_height;
        }
        add(L"BUTTON", L"Restore Defaults", BS_PUSHBUTTON | WS_TABSTOP, menu_left, y, menu_width,
            CONTROL_HEIGHT, ID_DEFAULTS, TAB_INPUT);
    }

    void build_graphics(int pane_width) {
        const int control_left = 200;
        const int control_width = pane_width - control_left - 16;
        const int top = 20;
        // How many pixels the renderer draws for each of the game's. First, because it is what
        // the setting below it is working with.
        label("Render scale", 16, top + 3, control_left - 26, LABEL_HEIGHT, TAB_GRAPHICS, false);
        render_scale_ = drop_list(control_left, top, control_width, ID_RENDER_SCALE, TAB_GRAPHICS);
        for (unsigned scale = MIN_RENDER_SCALE; scale <= MAX_RENDER_SCALE; ++scale) {
            wchar_t text[64];
            std::swprintf(text, 64, L"%u×  (%u×%u)", scale, 320 * scale, 240 * scale);
            add_item(render_scale_, text);
        }
        label("The game still computes everything in 320×240 and its sprites are still enlarged "
              "as whole blocks; what gets finer is where an edge lands, wherever the scene is "
              "transformed or scaled. Every step costs its square in work — the rasteriser is "
              "software, though it is drawn on every core — so raise it until the frame rate "
              "stops keeping up.",
              16, top + 34, pane_width - 32, 76, TAB_GRAPHICS, true);
        hi_res_text_ = check_box("Dialogue text at window resolution", control_left, top + 114,
                                 control_width, ID_HI_RES_TEXT, TAB_GRAPHICS);
        label("The game draws its dialogue as a bitmap font, one quad per letter. Enlarged, each "
              "letter is otherwise blurred across however far it was stretched; this reconstructs "
              "its edge at the render scale instead. It needs a scale above 1× — at 1× one texel "
              "is one pixel and there is no edge to resolve.",
              16, top + 136, pane_width - 32, 50, TAB_GRAPHICS, true);

        const int y = top + 194;
        label("Scaling", 16, y + 3, control_left - 26, LABEL_HEIGHT, TAB_GRAPHICS, false);
        scaling_ = drop_list(control_left, y, control_width, ID_SCALING, TAB_GRAPHICS);
        for (unsigned i = 0; i < SCALING_COUNT; ++i) {
            add_item(scaling_, wide(scaling_label(static_cast<Scaling>(i))));
        }
        label("How the picture is enlarged to fill a window many times its size, which is the "
              "last step and the only one the game has no say in. Sharp enlarges by whole blocks "
              "and then softens the fraction left over; Nearest leaves hard blocks of uneven "
              "size; Smooth blurs the lot.",
              16, y + 34, pane_width - 32, 62, TAB_GRAPHICS, true);
        pixel_perfect_ = check_box("Whole multiples only", control_left, y + 104, control_width,
                                   ID_PIXEL_PERFECT, TAB_GRAPHICS);
        label("Every pixel exactly the same size, at the cost of a border wherever the window is "
              "not a whole multiple of the picture.",
              16, y + 130, pane_width - 32, 34, TAB_GRAPHICS, true);
    }

    static int key_id(unsigned action, unsigned slot) {
        return ID_KEY_BASE + static_cast<int>(action * BINDING_SLOTS + slot);
    }

    // Cheats are on a tab of their own, and there is nothing else on it. Each one changes what
    // the game does rather than how this program shows it, which is a different kind of
    // decision, and among the display settings someone could turn one on while looking for
    // something else. The words are the Cocoa window's.
    void build_cheats(int pane_width) {
        const int y = 20;
        label("These change the game itself. None of them is written into your saved game.", 16, y,
              pane_width - 32, 18, TAB_CHEATS, true);
        unlock_chapters_ = check_box("Unlock all chapters", 16, y + 34, pane_width - 32,
                                     ID_UNLOCK_CHAPTERS, TAB_CHEATS);
        label("Play ▸ Select Chapter normally offers only the chapters you have reached. With this "
              "on it offers all nine, from The Arrival to The Escape, and starts whichever you "
              "pick. Your progress is not touched and nothing is written: turn it off and the list "
              "is back to what you have earned.",
              16, y + 60, pane_width - 32, 70, TAB_CHEATS, true);
    }

    // Every control is a child of the window, offset into the tab body; the tab it belongs to
    // decides whether it is shown.
    void show_tab(unsigned tab) {
        for (const Owned& owned : tab_of_) {
            ShowWindow(owned.control, owned.tab == tab ? SW_SHOW : SW_HIDE);
        }
    }

    // --- what the controls show ------------------------------------------------------------

    // Show what each action is bound to now, and the settings as they stand. The live settings
    // are read here rather than kept: the hooks were built before the saved settings were read,
    // so the copy in them is a snapshot of the defaults.
    void refresh() {
        const Settings& now = settings();
        hooks_.frame_rate = now.frame_rate;
        hooks_.show_frame_rate = now.show_frame_rate;
        hooks_.scaling = now.scaling;
        hooks_.pixel_perfect = now.pixel_perfect;
        hooks_.render_scale = now.render_scale;

        unsigned choice_count = 0;
        const InputChoice* choices = assignable_inputs(choice_count);
        const Action* order = all_actions();
        for (unsigned i = 0; i < ACTION_COUNT; ++i) {
            for (unsigned slot = 0; slot < BINDING_SLOTS; ++slot) {
                const InputCode code = input_bindings().code(order[i], slot);
                WPARAM index = 0;  // "nothing bound", unless the code is one this platform offers
                for (unsigned c = 0; c < choice_count; ++c) {
                    if (choices[c].code == code) {
                        index = c + 1;
                        break;
                    }
                }
                SendMessageW(key_menus_[i][slot], CB_SETCURSEL, index, 0);
            }
        }
        show_frame_rate(hooks_.frame_rate);
        SendMessageW(show_rate_, BM_SETCHECK, hooks_.show_frame_rate ? BST_CHECKED : BST_UNCHECKED,
                     0);
        SendMessageW(scaling_, CB_SETCURSEL, static_cast<WPARAM>(hooks_.scaling), 0);
        SendMessageW(pixel_perfect_, BM_SETCHECK,
                     hooks_.pixel_perfect ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(render_scale_, CB_SETCURSEL,
                     static_cast<WPARAM>(hooks_.render_scale - MIN_RENDER_SCALE), 0);
        hooks_.high_resolution_text = now.high_resolution_text;
        SendMessageW(hi_res_text_, BM_SETCHECK,
                     hooks_.high_resolution_text ? BST_CHECKED : BST_UNCHECKED, 0);
        // Greyed at 1x rather than left on and doing nothing: there is no glyph edge to resolve
        // when one texel is one pixel, and a switch that can be turned on to no effect is a
        // switch that says this feature does not work.
        EnableWindow(hi_res_text_, hooks_.render_scale > MIN_RENDER_SCALE);
        hooks_.unlock_all_chapters = now.unlock_all_chapters;
        SendMessageW(unlock_chapters_, BM_SETCHECK,
                     hooks_.unlock_all_chapters ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    void show_frame_rate(unsigned rate) {
        hooks_.frame_rate = rate;
        rate_choice_count = 3;
        rate_choices[0] = 30;
        rate_choices[1] = 60;
        rate_choices[2] = 0;
        if (rate != 0 && rate != 30 && rate != 60) {  // started with --fps= something else
            rate_choices[0] = rate;
            rate_choices[1] = 30;
            rate_choices[2] = 60;
            rate_choices[3] = 0;
            rate_choice_count = 4;
        }
        SendMessageW(frame_rate_, CB_RESETCONTENT, 0, 0);
        for (unsigned i = 0; i < rate_choice_count; ++i) {
            add_item(frame_rate_, rate_choices[i] == 0 ? std::wstring(L"Unlocked")
                                                       : std::to_wstring(rate_choices[i]));
            if (rate_choices[i] == rate) {
                SendMessageW(frame_rate_, CB_SETCURSEL, i, 0);
            }
        }
    }

    // --- what the player did ----------------------------------------------------------------

    [[nodiscard]] static int selected(HWND list) {
        return static_cast<int>(SendMessageW(list, CB_GETCURSEL, 0, 0));
    }

    [[nodiscard]] static bool checked(HWND box) {
        return SendMessageW(box, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    void frame_rate_chosen() {
        const int index = selected(frame_rate_);
        if (index < 0 || static_cast<unsigned>(index) >= rate_choice_count) {
            return;
        }
        // The host owns the setting: ask it to change, and it reports back through
        // settings_window_set_frame_rate, which is also what the L key goes through.
        if (hooks_.on_frame_rate_chosen != nullptr) {
            hooks_.on_frame_rate_chosen(hooks_.context, rate_choices[index]);
        } else {
            show_frame_rate(hooks_.frame_rate);  // nothing changed
        }
    }

    void show_frame_rate_chosen() {
        hooks_.show_frame_rate = checked(show_rate_);
        if (hooks_.on_show_frame_rate_changed != nullptr) {
            hooks_.on_show_frame_rate_changed(hooks_.context, hooks_.show_frame_rate);
        }
    }

    void scaling_chosen() {
        const int index = selected(scaling_);
        if (index < 0 || static_cast<unsigned>(index) >= SCALING_COUNT) {
            return;
        }
        hooks_.scaling = static_cast<Scaling>(index);
        if (hooks_.on_scaling_chosen != nullptr) {
            hooks_.on_scaling_chosen(hooks_.context, hooks_.scaling);
        }
    }

    void pixel_perfect_chosen() {
        hooks_.pixel_perfect = checked(pixel_perfect_);
        if (hooks_.on_pixel_perfect_changed != nullptr) {
            hooks_.on_pixel_perfect_changed(hooks_.context, hooks_.pixel_perfect);
        }
    }

    void render_scale_chosen() {
        const int index = selected(render_scale_);
        if (index < 0 || static_cast<unsigned>(index) > MAX_RENDER_SCALE - MIN_RENDER_SCALE) {
            return;
        }
        hooks_.render_scale = static_cast<unsigned>(index) + MIN_RENDER_SCALE;
        if (hooks_.on_render_scale_chosen != nullptr) {
            hooks_.on_render_scale_chosen(hooks_.context, hooks_.render_scale);
        }
        EnableWindow(hi_res_text_, hooks_.render_scale > MIN_RENDER_SCALE);
    }

    void high_resolution_text_chosen() {
        hooks_.high_resolution_text = checked(hi_res_text_);
        if (hooks_.on_high_resolution_text_changed != nullptr) {
            hooks_.on_high_resolution_text_changed(hooks_.context, hooks_.high_resolution_text);
        }
    }

    void unlock_all_chapters_chosen() {
        hooks_.unlock_all_chapters = checked(unlock_chapters_);
        if (hooks_.on_unlock_all_chapters_changed != nullptr) {
            hooks_.on_unlock_all_chapters_changed(hooks_.context, hooks_.unlock_all_chapters);
        }
    }

    void key_chosen(int id) {
        const unsigned which = static_cast<unsigned>(id - ID_KEY_BASE);
        const unsigned row = which / BINDING_SLOTS, slot = which % BINDING_SLOTS;
        if (row >= ACTION_COUNT) {
            return;
        }
        unsigned choice_count = 0;
        const InputChoice* choices = assignable_inputs(choice_count);
        const int index = selected(key_menus_[row][slot]);
        if (index < 0 || static_cast<unsigned>(index) > choice_count) {
            return;
        }
        const InputCode code =
            index == 0 ? NO_INPUT : choices[static_cast<unsigned>(index) - 1].code;
        const Action* order = all_actions();
        if (code == NO_INPUT) {
            input_bindings().clear(order[row], slot);
        } else {
            input_bindings().bind(order[row], code, slot);
        }
        // Binding takes the key away from whichever action had it, so every row may have changed.
        refresh();
        if (hooks_.on_bindings_changed != nullptr) {
            hooks_.on_bindings_changed(hooks_.context);
        }
    }

    void restore_defaults() {
        input_bindings().restore_defaults();
        refresh();
        if (hooks_.on_bindings_changed != nullptr) {
            hooks_.on_bindings_changed(hooks_.context);
        }
    }

    void command(int id, int notification) {
        if (id >= ID_KEY_BASE &&
            id < ID_KEY_BASE + static_cast<int>(ACTION_COUNT * BINDING_SLOTS)) {
            if (notification == CBN_SELCHANGE) {
                key_chosen(id);
            }
            return;
        }
        switch (id) {
        case ID_FRAME_RATE:
            if (notification == CBN_SELCHANGE) {
                frame_rate_chosen();
            }
            break;
        case ID_SHOW_RATE:
            if (notification == BN_CLICKED) {
                show_frame_rate_chosen();
            }
            break;
        case ID_RENDER_SCALE:
            if (notification == CBN_SELCHANGE) {
                render_scale_chosen();
            }
            break;
        case ID_SCALING:
            if (notification == CBN_SELCHANGE) {
                scaling_chosen();
            }
            break;
        case ID_PIXEL_PERFECT:
            if (notification == BN_CLICKED) {
                pixel_perfect_chosen();
            }
            break;
        case ID_DEFAULTS:
            if (notification == BN_CLICKED) {
                restore_defaults();
            }
            break;
        case ID_HI_RES_TEXT:
            if (notification == BN_CLICKED) {
                high_resolution_text_chosen();
            }
            break;
        case ID_UNLOCK_CHAPTERS:
            if (notification == BN_CLICKED) {
                unlock_all_chapters_chosen();
            }
            break;
        case IDCANCEL:  // Escape, from IsDialogMessage
            ShowWindow(window_, SW_HIDE);
            break;
        default:
            break;
        }
    }

    // --- painting --------------------------------------------------------------------------

    // The window's background: the system's, and over the tab body the tab theme's, which is
    // what the tab control paints there itself. This is also what a themed check box asks the
    // parent to paint behind it (WM_PRINTCLIENT), so the two have to agree.
    void paint_background(HDC hdc) const {
        RECT client{};
        GetClientRect(window_, &client);
        FillRect(hdc, &client, GetSysColorBrush(COLOR_BTNFACE));
        if (theme_ != nullptr) {
            RECT body = display_;
            DrawThemeBackground(theme_, hdc, THEME_TAB_BODY, 0, &body, nullptr);
        }
    }

    LRESULT handle(UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
        case WM_COMMAND:
            command(LOWORD(wparam), HIWORD(wparam));
            return 0;
        case WM_NOTIFY: {
            const NMHDR* header = reinterpret_cast<const NMHDR*>(lparam);
            if (header->hwndFrom == tabs_ && header->code == TCN_SELCHANGE) {
                show_tab(static_cast<unsigned>(TabCtrl_GetCurSel(tabs_)));
            }
            return 0;
        }
        case WM_ERASEBKGND:
            paint_background(reinterpret_cast<HDC>(wparam));
            return 1;
        case WM_PRINTCLIENT:
            paint_background(reinterpret_cast<HDC>(wparam));
            return 0;
        case WM_CTLCOLORSTATIC: {
            // Text over the tab body rather than in a box of its own colour: the body is
            // painted behind the label first, by asking this window to print it there — which is
            // what a themed check box does for itself — and the label then draws only its text.
            // Without a theme the body is the button face, and so is the label's.
            const HDC hdc = reinterpret_cast<HDC>(wparam);
            const HWND control = reinterpret_cast<HWND>(lparam);
            bool secondary = false;
            for (HWND candidate : secondary_) {
                secondary = secondary || candidate == control;
            }
            SetTextColor(hdc, GetSysColor(secondary ? COLOR_GRAYTEXT : COLOR_WINDOWTEXT));
            if (theme_ == nullptr) {
                SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
                return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
            }
            DrawThemeParentBackground(control, hdc, nullptr);
            SetBkMode(hdc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetStockObject(HOLLOW_BRUSH));
        }
        case WM_THEMECHANGED:
            if (theme_ != nullptr) {
                CloseThemeData(theme_);
            }
            theme_ = OpenThemeData(window_, L"Tab");
            InvalidateRect(window_, nullptr, TRUE);
            return 0;
        case WM_DPICHANGED:
            // The monitor changed under the window. Everything was laid out for the old one, so
            // the window is rebuilt for the new one, where the system suggests putting it.
            rebuild_at(reinterpret_cast<const RECT*>(lparam));
            return 0;
        case WM_CLOSE:
            ShowWindow(window_, SW_HIDE);  // kept, for the next time
            return 0;
        case WM_DESTROY:
            if (theme_ != nullptr) {
                CloseThemeData(theme_);
                theme_ = nullptr;
            }
            window_ = nullptr;
            tab_of_.clear();
            secondary_.clear();
            return 0;
        default:
            return DefWindowProcW(window_, message, wparam, lparam);
        }
    }

    void rebuild_at(const RECT* suggested) {
        RECT where = *suggested;
        DestroyWindow(window_);
        if (!create()) {
            return;
        }
        SetWindowPos(window_, nullptr, where.left, where.top, where.right - where.left,
                     where.bottom - where.top, SWP_NOZORDER | SWP_NOACTIVATE);
        refresh();
        ShowWindow(window_, SW_SHOW);
    }

    static LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam,
                                             LPARAM lparam) {
        SettingsWindow* self = nullptr;
        if (message == WM_NCCREATE) {
            const CREATESTRUCTW* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            self = static_cast<SettingsWindow*>(create->lpCreateParams);
            self->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        }
        if (self == nullptr || self->window_ != window) {
            return DefWindowProcW(window, message, wparam, lparam);
        }
        return self->handle(message, wparam, lparam);
    }

    // SDL's pump, before it dispatches a message: the dialog keys — Tab, Escape, the arrows —
    // are ours to handle for our window, and a message IsDialogMessage took is not to be
    // dispatched again.
    static bool SDLCALL message_hook(void* userdata, MSG* msg) {
        const SettingsWindow* self = static_cast<const SettingsWindow*>(userdata);
        if (self->window_ == nullptr || !IsWindowVisible(self->window_)) {
            return true;
        }
        return !IsDialogMessageW(self->window_, msg);
    }

    struct Owned {
        HWND control;
        unsigned tab;
    };

    SettingsHooks hooks_;
    HWND window_ = nullptr;
    HWND tabs_ = nullptr;
    HFONT font_ = nullptr;
    HTHEME theme_ = nullptr;
    UINT dpi_ = 96;
    RECT display_{};  // the tab body, in the window's client pixels
    int pane_left_ = 0, pane_top_ = 0;
    std::vector<Owned> tab_of_;
    std::vector<HWND> secondary_;
    HWND key_menus_[ACTION_COUNT][BINDING_SLOTS] = {};
    HWND frame_rate_ = nullptr, show_rate_ = nullptr;
    HWND render_scale_ = nullptr, scaling_ = nullptr, pixel_perfect_ = nullptr;
    HWND hi_res_text_ = nullptr;
    HWND unlock_chapters_ = nullptr;
};

SettingsWindow& the_window() {
    static SettingsWindow window;
    return window;
}

}  // namespace

void settings_window_install(const SettingsHooks& hooks) {
    the_window().install(hooks);
}

void settings_window_open() {
    the_window().open();
}

void settings_window_set_frame_rate(unsigned frames_per_second) {
    the_window().set_frame_rate(frames_per_second);
}

}  // namespace lost::platform

#endif  // _WIN32
