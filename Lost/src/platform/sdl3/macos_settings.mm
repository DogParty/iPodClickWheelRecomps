// Cocoa side of macos_settings.h.
//
// Four tabs. General holds the frame rate and the title-bar readout; Input is a column of rows,
// one per action, each with a pop-up menu of the keys this platform is willing to assign
// (platform::assignable_inputs); Graphics chooses how many pixels are drawn and how they are
// enlarged; Cheats holds the switches that change what the game itself does.
//
// Choosing a key from a list rather than asking the player to press one is deliberate: a "press
// any key" prompt has to intercept the keyboard, and on macOS that means either the responder
// chain — which does not deliver keys to a button unless Full Keyboard Access is on — or an event
// monitor that has to be right about every case. A pop-up needs none of that and cannot fail
// quietly.
#include "platform/sdl3/macos_settings.h"

#include "platform/input_bindings.h"
#include "platform/settings.h"

#import <Cocoa/Cocoa.h>

#include <algorithm>

using lost::platform::Action;
using lost::platform::ACTION_COUNT;
using lost::platform::action_label;
using lost::platform::all_actions;
using lost::platform::assignable_inputs;
using lost::platform::BINDING_SLOTS;
using lost::platform::input_bindings;
using lost::platform::InputChoice;
using lost::platform::InputCode;
using lost::platform::MAX_RENDER_SCALE;
using lost::platform::MIN_RENDER_SCALE;
using lost::platform::NO_INPUT;
using lost::platform::Scaling;
using lost::platform::SCALING_COUNT;
using lost::platform::scaling_label;
using lost::platform::Settings;
using lost::platform::settings;
using lost::platform::SettingsHooks;

// Its own class so the row it belongs to is an ordinary property. `tag` was doing that job and
// also carrying each item's key code, which is one meaning too many for one word.
@interface MinigolfKeyMenu : NSPopUpButton
@property(nonatomic, assign) unsigned actionIndex;
@property(nonatomic, assign) unsigned slot;
@end

@implementation MinigolfKeyMenu
@end

@interface MinigolfSettingsWindow : NSWindowController
@property(nonatomic, assign) SettingsHooks hooks;
- (NSView*)makeCheatsPane:(NSSize)pane;
- (void)refresh;
- (void)keyChosen:(MinigolfKeyMenu*)sender;
- (void)restoreDefaults:(id)sender;
- (void)frameRateChosen:(id)sender;
- (void)scalingChosen:(id)sender;
- (void)pixelPerfectChosen:(id)sender;
- (void)renderScaleChosen:(id)sender;
- (void)highResolutionTextChosen:(id)sender;
- (void)unlockAllChaptersChosen:(id)sender;
- (void)showFrameRateChosen:(id)sender;
- (void)showFrameRate:(unsigned)rate;
@end

static MinigolfSettingsWindow* settings_window = nil;
static MinigolfKeyMenu* key_menus[ACTION_COUNT][BINDING_SLOTS];
static NSPopUpButton* frame_rate_menu = nil;
static NSButton* show_rate_switch = nil;

// What the frame-rate menu offers, in the order the items appear: 30, 60, and 0 for unlocked.
// A rate the game was started with (--fps=) that is none of those joins the front rather than
// being quietly rounded to one that is.
static unsigned rate_choices[4] = {30, 60, 0, 0};
static unsigned rate_choice_count = 3;
static NSPopUpButton* scaling_menu = nil;
static NSButton* pixel_perfect_switch = nil;
static NSPopUpButton* render_scale_menu = nil;
static NSButton* hi_res_text_switch = nil;
static NSButton* unlock_chapters_switch = nil;

// The plain, unbordered labels this window is mostly made of.
static NSTextField* make_label(NSRect frame, NSString* text, bool secondary) {
    NSTextField* field = [[NSTextField alloc] initWithFrame:frame];
    [field setStringValue:text];
    [field setBezeled:NO];
    [field setDrawsBackground:NO];
    [field setEditable:NO];
    [field setSelectable:NO];
    [field setUsesSingleLineMode:NO];
    [field setLineBreakMode:NSLineBreakByWordWrapping];
    if (secondary) {
        [field setFont:[NSFont systemFontOfSize:11]];
        [field setTextColor:[NSColor secondaryLabelColor]];
    }
    return field;
}

// The Input tab's geometry, needed in two places: laying the tab out, and sizing the window that
// has to hold it.
constexpr CGFloat INPUT_ROW_HEIGHT = 30;
constexpr CGFloat PANE_INSET = 12;

// The Graphics tab's, for the same reason: four controls, each with a note under it, and the
// notes are the tall part. `graphics_pane_height` adds up exactly what `makeGraphicsPane` lays
// out, so a fifth control cannot quietly fall off the bottom of that tab either.
constexpr CGFloat GRAPHICS_TOP_INSET = 40;
constexpr CGFloat GRAPHICS_ROW_HEIGHT = 26;
constexpr CGFloat GRAPHICS_NOTE_GAP = 14;
constexpr CGFloat GRAPHICS_NOTES[] = {62, 62, 62, 34};

CGFloat graphics_pane_height() {
    CGFloat height = GRAPHICS_TOP_INSET;
    for (const CGFloat note : GRAPHICS_NOTES) {
        height += GRAPHICS_ROW_HEIGHT + note + GRAPHICS_NOTE_GAP;
    }
    return height + 16;
}

// One row per control, plus the line of explanation above them and the Restore Defaults button
// below, plus the inset at the top and a matching one at the bottom.
CGFloat input_pane_height() {
    return 32 + INPUT_ROW_HEIGHT * (1 + ACTION_COUNT + 1) + 16;
}

@implementation MinigolfSettingsWindow

- (instancetype)init {
    const CGFloat width = 540;
    // The tallest tab decides, and how tall each is depends on how many controls it has — Input
    // grew by four when the wheel's four sides became bindable, and Graphics by two when the
    // render scale arrived. Working it out rather than writing it down means the next control
    // added cannot quietly fall off the bottom.
    const CGFloat height = std::max<CGFloat>(
        {380, input_pane_height() + 2 * PANE_INSET, graphics_pane_height() + 2 * PANE_INSET});
    NSWindow* window =
        [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, width, height)
                                    styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                      backing:NSBackingStoreBuffered
                                        defer:NO];
    [window setTitle:@"Settings"];
    [window center];
    [window setReleasedWhenClosed:NO];

    self = [super initWithWindow:window];
    if (self == nil) {
        return nil;
    }

    NSTabView* tabs =
        [[NSTabView alloc] initWithFrame:NSMakeRect(PANE_INSET, PANE_INSET, width - 2 * PANE_INSET,
                                                    height - 2 * PANE_INSET)];
    [[window contentView] addSubview:tabs];

    // The tab view decides how much room a tab actually has; ask it rather than guess, or every
    // row is a few points out from wherever the system draws the tabs this year.
    const NSSize pane = [tabs contentRect].size;
    [self addTab:tabs titled:@"General" view:[self makeGeneralPane:pane]];
    [self addTab:tabs titled:@"Input" view:[self makeInputPane:pane]];
    [self addTab:tabs titled:@"Graphics" view:[self makeGraphicsPane:pane]];
    [self addTab:tabs titled:@"Cheats" view:[self makeCheatsPane:pane]];

    [self refresh];
    return self;
}

- (void)addTab:(NSTabView*)tabs titled:(NSString*)title view:(NSView*)view {
    NSTabViewItem* item = [[NSTabViewItem alloc] initWithIdentifier:title];
    [item setLabel:title];
    [item setView:view];
    [tabs addTabViewItem:item];
}

- (NSView*)makeGeneralPane:(NSSize)pane {
    NSView* view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, pane.width, pane.height)];
    const CGFloat control_left = 200;
    const CGFloat control_width = pane.width - control_left - 16;
    CGFloat y = pane.height - 40;

    // A text field draws its text at the top of its frame, so each control's frame is placed by
    // its bottom edge, below the one before it.
    [view addSubview:make_label(NSMakeRect(16, y, control_left - 26, 20), @"FPS", false)];
    frame_rate_menu =
        [[NSPopUpButton alloc] initWithFrame:NSMakeRect(control_left, y - 4, control_width, 26)
                                   pullsDown:NO];
    [frame_rate_menu setTarget:self];
    [frame_rate_menu setAction:@selector(frameRateChosen:)];
    [view addSubview:frame_rate_menu];

    [view addSubview:make_label(NSMakeRect(16, y - 46, pane.width - 32, 32),
                                @"Frames are paced to this rate. Unlocked, the game runs as fast "
                                @"as the machine allows, which fast-forwards it.",
                                true)];

    show_rate_switch =
        [[NSButton alloc] initWithFrame:NSMakeRect(control_left, y - 84, control_width, 20)];
    [show_rate_switch setButtonType:NSButtonTypeSwitch];
    [show_rate_switch setTitle:@"Show FPS in the title bar"];
    [show_rate_switch setTarget:self];
    [show_rate_switch setAction:@selector(showFrameRateChosen:)];
    [view addSubview:show_rate_switch];
    return view;
}

- (NSView*)makeInputPane:(NSSize)pane {
    NSView* view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, pane.width, pane.height)];
    const CGFloat row_height = INPUT_ROW_HEIGHT;
    const CGFloat menu_left = 150;
    const CGFloat menu_width = (pane.width - menu_left - 16 - 10) / BINDING_SLOTS;
    CGFloat y = pane.height - 32;

    [view addSubview:make_label(NSMakeRect(16, y, pane.width - 32, 18),
                                @"Choose the keys for each control. Either one does it; changes "
                                @"apply straight away.",
                                true)];
    y -= row_height;

    unsigned choice_count = 0;
    const InputChoice* choices = assignable_inputs(choice_count);
    const Action* order = all_actions();
    for (unsigned i = 0; i < ACTION_COUNT; ++i) {
        [view addSubview:make_label(NSMakeRect(16, y, menu_left - 26, 20),
                                    @(action_label(order[i])), false)];

        // Item 0 is "nothing bound"; item c + 1 is choices[c]. Its position in the menu is the
        // only thing that maps an item to a key, so there is nothing to get out of step.
        for (unsigned slot = 0; slot < BINDING_SLOTS; ++slot) {
            const CGFloat left = menu_left + static_cast<CGFloat>(slot) * (menu_width + 10);
            MinigolfKeyMenu* menu =
                [[MinigolfKeyMenu alloc] initWithFrame:NSMakeRect(left, y - 4, menu_width, 26)
                                             pullsDown:NO];
            [menu addItemWithTitle:@"—"];
            for (unsigned c = 0; c < choice_count; ++c) {
                [menu addItemWithTitle:@(choices[c].label)];
            }
            menu.actionIndex = i;
            menu.slot = slot;
            [menu setTarget:self];
            [menu setAction:@selector(keyChosen:)];
            [view addSubview:menu];
            key_menus[i][slot] = menu;
        }
        y -= row_height;
    }

    NSButton* defaults =
        [[NSButton alloc] initWithFrame:NSMakeRect(menu_left, y - 4, menu_width, 26)];
    [defaults setBezelStyle:NSBezelStyleRounded];
    [defaults setTitle:@"Restore Defaults"];
    [defaults setTarget:self];
    [defaults setAction:@selector(restoreDefaults:)];
    [view addSubview:defaults];
    return view;
}

- (NSView*)makeGraphicsPane:(NSSize)pane {
    NSView* view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, pane.width, pane.height)];
    const CGFloat control_left = 200;
    const CGFloat control_width = pane.width - control_left - 16;
    // A cursor down the pane rather than four offsets from the top: this tab gained two controls
    // above the two it had, and every fixed offset below them would have had to move. `explain`
    // walks it past a note whose height it takes from GRAPHICS_NOTES, which is the same list
    // `graphics_pane_height` adds up.
    CGFloat y = pane.height - GRAPHICS_TOP_INSET;
    unsigned note = 0;
    const auto explain = [&](NSString* text) {
        const CGFloat height = GRAPHICS_NOTES[note++];
        y -= GRAPHICS_ROW_HEIGHT;
        [view addSubview:make_label(NSMakeRect(16, y - height + 20, pane.width - 32, height), text,
                                    true)];
        y -= height + GRAPHICS_NOTE_GAP;
    };

    // How many pixels the renderer draws for each of the game's. First, because it is what the
    // setting under it is working with.
    [view addSubview:make_label(NSMakeRect(16, y, control_left - 26, 20), @"Render scale", false)];
    render_scale_menu =
        [[NSPopUpButton alloc] initWithFrame:NSMakeRect(control_left, y - 4, control_width, 26)
                                   pullsDown:NO];
    for (unsigned scale = MIN_RENDER_SCALE; scale <= MAX_RENDER_SCALE; ++scale) {
        [render_scale_menu addItemWithTitle:[NSString stringWithFormat:@"%u×  (%u×%u)", scale,
                                                                       320 * scale, 240 * scale]];
    }
    [render_scale_menu setTarget:self];
    [render_scale_menu setAction:@selector(renderScaleChosen:)];
    [view addSubview:render_scale_menu];
    explain(@"The game still computes everything in 320×240 and its sprites are still enlarged as "
            @"whole blocks; what gets finer is where an edge lands, wherever the scene is "
            @"transformed or scaled. Every step costs its square in work — the rasteriser is "
            @"software, though it is drawn on every core — so raise it until the frame rate "
            @"stops keeping up.");

    hi_res_text_switch =
        [[NSButton alloc] initWithFrame:NSMakeRect(control_left, y - 2, control_width, 20)];
    [hi_res_text_switch setButtonType:NSButtonTypeSwitch];
    [hi_res_text_switch setTitle:@"Dialogue text at window resolution"];
    [hi_res_text_switch setTarget:self];
    [hi_res_text_switch setAction:@selector(highResolutionTextChosen:)];
    [view addSubview:hi_res_text_switch];
    explain(@"The game draws its dialogue as a bitmap font, one quad per letter. Enlarged, each "
            @"letter is otherwise blurred across however far it was stretched; this reconstructs "
            @"its edge at the render scale instead. It needs a scale above 1× — at 1× one texel "
            @"is one pixel and there is no edge to resolve.");

    [view addSubview:make_label(NSMakeRect(16, y, control_left - 26, 20), @"Scaling", false)];
    scaling_menu =
        [[NSPopUpButton alloc] initWithFrame:NSMakeRect(control_left, y - 4, control_width, 26)
                                   pullsDown:NO];
    for (unsigned i = 0; i < SCALING_COUNT; ++i) {
        [scaling_menu addItemWithTitle:@(scaling_label(static_cast<Scaling>(i)))];
    }
    [scaling_menu setTarget:self];
    [scaling_menu setAction:@selector(scalingChosen:)];
    [view addSubview:scaling_menu];
    explain(@"How the picture is enlarged to fill a window many times its size, which is the last "
            @"step and the only one the game has no say in. Sharp enlarges by whole blocks and "
            @"then softens the fraction left over; Nearest leaves hard blocks of uneven size; "
            @"Smooth blurs the lot.");

    pixel_perfect_switch =
        [[NSButton alloc] initWithFrame:NSMakeRect(control_left, y - 2, control_width, 20)];
    [pixel_perfect_switch setButtonType:NSButtonTypeSwitch];
    [pixel_perfect_switch setTitle:@"Whole multiples only"];
    [pixel_perfect_switch setTarget:self];
    [pixel_perfect_switch setAction:@selector(pixelPerfectChosen:)];
    [view addSubview:pixel_perfect_switch];
    explain(@"Every pixel exactly the same size, at the cost of a border wherever the window is "
            @"not a whole multiple of the picture.");
    return view;
}

// Cheats are on a tab of their own, and there is nothing else on it. Each one changes what the
// game does rather than how this program shows it, which is a different kind of decision, and
// among the display settings someone could turn one on while looking for something else.
- (NSView*)makeCheatsPane:(NSSize)pane {
    NSView* view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, pane.width, pane.height)];
    const CGFloat y = pane.height - GRAPHICS_TOP_INSET;

    [view addSubview:make_label(NSMakeRect(16, y, pane.width - 32, 18),
                                @"These change the game itself. None of them is written into your "
                                @"saved game.",
                                true)];

    unlock_chapters_switch =
        [[NSButton alloc] initWithFrame:NSMakeRect(16, y - 34, pane.width - 32, 20)];
    [unlock_chapters_switch setButtonType:NSButtonTypeSwitch];
    [unlock_chapters_switch setTitle:@"Unlock all chapters"];
    [unlock_chapters_switch setTarget:self];
    [unlock_chapters_switch setAction:@selector(unlockAllChaptersChosen:)];
    [view addSubview:unlock_chapters_switch];

    [view addSubview:make_label(NSMakeRect(16, y - 112, pane.width - 32, 62),
                                @"Play ▸ Select Chapter normally offers only the chapters you "
                                @"have reached. With this on it offers all nine, from The Arrival "
                                @"to The Escape, and starts whichever you pick. Your progress is "
                                @"not touched and nothing is written: turn it off and the list is "
                                @"back to what you have earned.",
                                true)];
    return view;
}

// Show what each action is bound to now, and what every setting now *is*.
//
// The values come from `settings()` rather than from the hooks, and that is a fix rather than a
// preference. `macos_settings_install` is called from the platform's constructor, which runs
// before the saved settings have been read — `create_platform` is the first thing the frame pump
// does and `load_settings` is sixty lines later — so the copy in the hooks is a snapshot of the
// *defaults*, taken before the player's file was opened. The window showed those defaults every
// time it was opened, whatever the game was actually doing. The frame rate escaped it only
// because `apply_settings` pushes that one value back through
// `macos_settings_set_frame_rate` afterwards; nothing else had such a path.
//
// A binding this platform does not offer in its list still shows, as an extra item, so opening
// the window cannot silently discard it.
- (void)refresh {
    // The live settings, over whatever the hooks were built with. The callbacks in `hooks` are
    // the half that matters and they are left alone.
    {
        const Settings& now = settings();
        SettingsHooks live = self.hooks;
        live.frame_rate = now.frame_rate;
        live.show_frame_rate = now.show_frame_rate;
        live.scaling = now.scaling;
        live.pixel_perfect = now.pixel_perfect;
        live.render_scale = now.render_scale;
        live.high_resolution_text = now.high_resolution_text;
        live.unlock_all_chapters = now.unlock_all_chapters;
        self.hooks = live;
    }
    unsigned choice_count = 0;
    const InputChoice* choices = assignable_inputs(choice_count);
    const Action* order = all_actions();
    for (unsigned i = 0; i < ACTION_COUNT; ++i) {
        for (unsigned slot = 0; slot < BINDING_SLOTS; ++slot) {
            const InputCode code = input_bindings().code(order[i], slot);
            NSInteger index = 0;  // "nothing bound", unless the code is one this platform offers
            for (unsigned c = 0; c < choice_count; ++c) {
                if (choices[c].code == code) {
                    index = static_cast<NSInteger>(c) + 1;
                    break;
                }
            }
            [key_menus[i][slot] selectItemAtIndex:index];
        }
    }
    [self showFrameRate:self.hooks.frame_rate];
    [show_rate_switch
        setState:self.hooks.show_frame_rate ? NSControlStateValueOn : NSControlStateValueOff];
    [scaling_menu selectItemAtIndex:static_cast<NSInteger>(self.hooks.scaling)];
    [pixel_perfect_switch
        setState:self.hooks.pixel_perfect ? NSControlStateValueOn : NSControlStateValueOff];
    [render_scale_menu
        selectItemAtIndex:static_cast<NSInteger>(self.hooks.render_scale - MIN_RENDER_SCALE)];
    [hi_res_text_switch
        setState:self.hooks.high_resolution_text ? NSControlStateValueOn : NSControlStateValueOff];
    // Greyed at 1x rather than left on and doing nothing: there is no glyph edge to resolve when
    // one texel is one pixel, and a switch that can be turned on to no effect is a switch that
    // says this feature does not work.
    [hi_res_text_switch setEnabled:self.hooks.render_scale > MIN_RENDER_SCALE];
    [unlock_chapters_switch
        setState:self.hooks.unlock_all_chapters ? NSControlStateValueOn : NSControlStateValueOff];
}

- (void)showFrameRate:(unsigned)rate {
    SettingsHooks hooks = self.hooks;
    hooks.frame_rate = rate;
    self.hooks = hooks;

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
    [frame_rate_menu removeAllItems];
    for (unsigned i = 0; i < rate_choice_count; ++i) {
        [frame_rate_menu addItemWithTitle:rate_choices[i] == 0
                                              ? @"Unlocked"
                                              : [NSString stringWithFormat:@"%u", rate_choices[i]]];
        if (rate_choices[i] == rate) {
            [frame_rate_menu selectItemAtIndex:static_cast<NSInteger>(i)];
        }
    }
}

- (void)frameRateChosen:(id)sender {
    (void)sender;
    const NSInteger selected = [frame_rate_menu indexOfSelectedItem];
    if (selected < 0 || static_cast<unsigned>(selected) >= rate_choice_count) {
        return;
    }
    // The host owns the setting: ask it to change, and it reports back through
    // macos_settings_set_frame_rate, which is also what the L key goes through.
    if (self.hooks.on_frame_rate_chosen != nullptr) {
        self.hooks.on_frame_rate_chosen(self.hooks.context, rate_choices[selected]);
    } else {
        [self showFrameRate:self.hooks.frame_rate];  // nothing changed
    }
}

- (void)showFrameRateChosen:(id)sender {
    (void)sender;
    const bool show = [show_rate_switch state] == NSControlStateValueOn;
    SettingsHooks hooks = self.hooks;
    hooks.show_frame_rate = show;
    self.hooks = hooks;
    if (hooks.on_show_frame_rate_changed != nullptr) {
        hooks.on_show_frame_rate_changed(hooks.context, show);
    }
}

- (void)scalingChosen:(id)sender {
    (void)sender;
    const NSInteger selected = [scaling_menu indexOfSelectedItem];
    if (selected < 0 || static_cast<unsigned>(selected) >= SCALING_COUNT) {
        return;
    }
    const Scaling scaling = static_cast<Scaling>(selected);
    SettingsHooks hooks = self.hooks;
    hooks.scaling = scaling;
    self.hooks = hooks;
    if (hooks.on_scaling_chosen != nullptr) {
        hooks.on_scaling_chosen(hooks.context, scaling);
    }
}

- (void)pixelPerfectChosen:(id)sender {
    (void)sender;
    const bool whole = [pixel_perfect_switch state] == NSControlStateValueOn;
    SettingsHooks hooks = self.hooks;
    hooks.pixel_perfect = whole;
    self.hooks = hooks;
    if (hooks.on_pixel_perfect_changed != nullptr) {
        hooks.on_pixel_perfect_changed(hooks.context, whole);
    }
}

- (void)renderScaleChosen:(id)sender {
    (void)sender;
    const NSInteger selected = [render_scale_menu indexOfSelectedItem];
    if (selected < 0 || static_cast<unsigned>(selected) > MAX_RENDER_SCALE - MIN_RENDER_SCALE) {
        return;
    }
    const unsigned scale = static_cast<unsigned>(selected) + MIN_RENDER_SCALE;
    SettingsHooks hooks = self.hooks;
    hooks.render_scale = scale;
    self.hooks = hooks;
    if (hooks.on_render_scale_chosen != nullptr) {
        hooks.on_render_scale_chosen(hooks.context, scale);
    }
    // The text switch is only meaningful above 1x, and the scale has just changed.
    [hi_res_text_switch setEnabled:scale > MIN_RENDER_SCALE];
}

- (void)highResolutionTextChosen:(id)sender {
    (void)sender;
    const bool resolve = [hi_res_text_switch state] == NSControlStateValueOn;
    SettingsHooks hooks = self.hooks;
    hooks.high_resolution_text = resolve;
    self.hooks = hooks;
    if (hooks.on_high_resolution_text_changed != nullptr) {
        hooks.on_high_resolution_text_changed(hooks.context, resolve);
    }
}

- (void)unlockAllChaptersChosen:(id)sender {
    (void)sender;
    const bool unlock = [unlock_chapters_switch state] == NSControlStateValueOn;
    SettingsHooks hooks = self.hooks;
    hooks.unlock_all_chapters = unlock;
    self.hooks = hooks;
    if (hooks.on_unlock_all_chapters_changed != nullptr) {
        hooks.on_unlock_all_chapters_changed(hooks.context, unlock);
    }
}

- (void)keyChosen:(MinigolfKeyMenu*)sender {
    const unsigned row = sender.actionIndex;
    const unsigned slot = sender.slot;
    unsigned choice_count = 0;
    const InputChoice* choices = assignable_inputs(choice_count);
    const NSInteger selected = [sender indexOfSelectedItem];
    if (row >= ACTION_COUNT || slot >= BINDING_SLOTS || selected < 0) {
        return;
    }
    const InputCode code =
        selected == 0 ? NO_INPUT : choices[static_cast<unsigned>(selected) - 1].code;
    const Action* order = all_actions();
    if (code == NO_INPUT) {
        input_bindings().clear(order[row], slot);
    } else {
        input_bindings().bind(order[row], code, slot);
    }
    // Binding takes the key away from whichever action had it, so every row may have changed.
    [self refresh];
    if (self.hooks.on_bindings_changed != nullptr) {
        self.hooks.on_bindings_changed(self.hooks.context);
    }
}

- (void)restoreDefaults:(id)sender {
    (void)sender;
    input_bindings().restore_defaults();
    [self refresh];
    if (self.hooks.on_bindings_changed != nullptr) {
        self.hooks.on_bindings_changed(self.hooks.context);
    }
}

@end

@interface MinigolfSettingsMenuTarget : NSObject
- (void)openSettings:(id)sender;
@end

@implementation MinigolfSettingsMenuTarget
- (void)openSettings:(id)sender {
    (void)sender;
    lost::platform::macos_settings_open();
}
@end

namespace lost::platform {

namespace {
MinigolfSettingsMenuTarget* menu_target = nil;
SettingsHooks installed_hooks;
}  // namespace

void macos_settings_install(const SettingsHooks& hooks) {
    @autoreleasepool {
        installed_hooks = hooks;

        NSMenu* main_menu = [NSApp mainMenu];
        if (main_menu == nil || [main_menu numberOfItems] == 0) {
            return;
        }
        // The application menu is the first one, and Settings belongs near the top of it, where
        // every other Mac application keeps it.
        NSMenu* application_menu = [[main_menu itemAtIndex:0] submenu];
        if (application_menu == nil) {
            return;
        }
        menu_target = [[MinigolfSettingsMenuTarget alloc] init];
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:@"Settings…"
                                                      action:@selector(openSettings:)
                                               keyEquivalent:@","];
        // Spelled out rather than left to the default, so the item really is ⌘, and not , alone.
        [item setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
        [item setTarget:menu_target];
        [application_menu insertItem:item atIndex:1];
        [application_menu insertItem:[NSMenuItem separatorItem] atIndex:2];
    }
}

void macos_settings_open() {
    @autoreleasepool {
        if (settings_window == nil) {
            settings_window = [[MinigolfSettingsWindow alloc] init];
        }
        settings_window.hooks = installed_hooks;
        [settings_window refresh];
        [settings_window showWindow:nil];
        [[settings_window window] makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
    }
}

void macos_settings_set_frame_rate(unsigned frames_per_second) {
    installed_hooks.frame_rate = frames_per_second;
    if (settings_window != nil) {
        [settings_window showFrameRate:frames_per_second];
    }
}

}  // namespace lost::platform
