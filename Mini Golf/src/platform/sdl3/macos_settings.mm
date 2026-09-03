// Cocoa side of settings_window.h.
//
// Three tabs. General holds the frame rate and the title-bar readout; Input is a column of rows,
// one per action, each with a pop-up menu of the keys this platform is willing to assign
// (platform::assignable_inputs); Graphics chooses how the picture is enlarged.
//
// Choosing a key from a list rather than asking the player to press one is deliberate: a "press
// any key" prompt has to intercept the keyboard, and on macOS that means either the responder
// chain — which does not deliver keys to a button unless Full Keyboard Access is on — or an event
// monitor that has to be right about every case. A pop-up needs none of that and cannot fail
// quietly.
#include "platform/sdl3/settings_window.h"

#include "platform/input_bindings.h"
#include "platform/settings.h"

#import <Cocoa/Cocoa.h>

using minigolf::platform::Action;
using minigolf::platform::ACTION_COUNT;
using minigolf::platform::action_label;
using minigolf::platform::all_actions;
using minigolf::platform::assignable_inputs;
using minigolf::platform::BINDING_SLOTS;
using minigolf::platform::input_bindings;
using minigolf::platform::InputChoice;
using minigolf::platform::InputCode;
using minigolf::platform::MAX_RENDER_SCALE;
using minigolf::platform::MIN_RENDER_SCALE;
using minigolf::platform::NO_INPUT;
using minigolf::platform::Scaling;
using minigolf::platform::SCALING_COUNT;
using minigolf::platform::scaling_label;
using minigolf::platform::Settings;
using minigolf::platform::settings;
using minigolf::platform::SettingsHooks;

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
- (void)refresh;
- (void)keyChosen:(MinigolfKeyMenu*)sender;
- (void)restoreDefaults:(id)sender;
- (void)frameRateChosen:(id)sender;
- (void)scalingChosen:(id)sender;
- (void)pixelPerfectChosen:(id)sender;
- (void)renderScaleChosen:(id)sender;
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

@implementation MinigolfSettingsWindow

- (instancetype)init {
    const CGFloat width = 540;
    const CGFloat height = 380;  // set by the tallest tab, which is Input
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

    NSTabView* tabs = [[NSTabView alloc] initWithFrame:NSMakeRect(12, 12, width - 24, height - 24)];
    [[window contentView] addSubview:tabs];

    // The tab view decides how much room a tab actually has; ask it rather than guess, or every
    // row is a few points out from wherever the system draws the tabs this year.
    const NSSize pane = [tabs contentRect].size;
    [self addTab:tabs titled:@"General" view:[self makeGeneralPane:pane]];
    [self addTab:tabs titled:@"Input" view:[self makeInputPane:pane]];
    [self addTab:tabs titled:@"Graphics" view:[self makeGraphicsPane:pane]];

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
    const CGFloat row_height = 30;
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
    const CGFloat top = pane.height - 40;

    // How many pixels the renderer draws for each of the game's. First, because it is what the
    // setting below it is working with.
    [view
        addSubview:make_label(NSMakeRect(16, top, control_left - 26, 20), @"Render scale", false)];
    render_scale_menu =
        [[NSPopUpButton alloc] initWithFrame:NSMakeRect(control_left, top - 4, control_width, 26)
                                   pullsDown:NO];
    for (unsigned scale = MIN_RENDER_SCALE; scale <= MAX_RENDER_SCALE; ++scale) {
        [render_scale_menu addItemWithTitle:[NSString stringWithFormat:@"%u×  (%u×%u)", scale,
                                                                       320 * scale, 240 * scale]];
    }
    [render_scale_menu setTarget:self];
    [render_scale_menu setAction:@selector(renderScaleChosen:)];
    [view addSubview:render_scale_menu];

    [view addSubview:make_label(NSMakeRect(16, top - 84, pane.width - 32, 70),
                                @"The game still computes everything in 320×240 and its sprites "
                                @"are still enlarged as whole blocks; what gets finer is where an "
                                @"edge lands, wherever the course is transformed or scaled. Every "
                                @"step costs its square in work — the rasteriser is software, "
                                @"though it is drawn on every core — so raise it until the frame "
                                @"rate stops keeping up.",
                                true)];

    const CGFloat y = top - 128;
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

    [view addSubview:make_label(NSMakeRect(16, y - 76, pane.width - 32, 62),
                                @"The game draws 320×240 pixels and no more, so this is about the "
                                @"last step only. Sharp enlarges by whole blocks and then softens "
                                @"the fraction left over; Nearest leaves hard blocks of uneven "
                                @"size; Smooth blurs the lot.",
                                true)];

    pixel_perfect_switch =
        [[NSButton alloc] initWithFrame:NSMakeRect(control_left, y - 114, control_width, 20)];
    [pixel_perfect_switch setButtonType:NSButtonTypeSwitch];
    [pixel_perfect_switch setTitle:@"Whole multiples only"];
    [pixel_perfect_switch setTarget:self];
    [pixel_perfect_switch setAction:@selector(pixelPerfectChosen:)];
    [view addSubview:pixel_perfect_switch];

    [view addSubview:make_label(NSMakeRect(16, y - 160, pane.width - 32, 32),
                                @"Every pixel exactly the same size, at the cost of a border "
                                @"wherever the window is not a whole multiple of 320×240.",
                                true)];
    return view;
}

// Show what each action is bound to now. A binding this platform does not offer in its list
// still shows, as an extra item, so opening the window cannot silently discard it.
- (void)refresh {
    // The live settings, over whatever the hooks were built with. `settings_window_install` runs
    // from the platform's constructor, before the saved settings have been read, so the copy in
    // the hooks is a snapshot of the *defaults* — the window showed those every time it was
    // opened, whatever the game was actually doing. The frame rate escaped it only because
    // `apply_settings` pushes that one value back afterwards. The callbacks in `hooks` are the
    // half that matters and are left alone.
    {
        const Settings& now = settings();
        SettingsHooks live = self.hooks;
        live.frame_rate = now.frame_rate;
        live.show_frame_rate = now.show_frame_rate;
        live.scaling = now.scaling;
        live.pixel_perfect = now.pixel_perfect;
        live.render_scale = now.render_scale;
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
    // settings_window_set_frame_rate, which is also what the L key goes through.
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
    minigolf::platform::settings_window_open();
}
@end

namespace minigolf::platform {

namespace {
MinigolfSettingsMenuTarget* menu_target = nil;
SettingsHooks installed_hooks;
}  // namespace

void settings_window_install(const SettingsHooks& hooks) {
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

void settings_window_open() {
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

void settings_window_set_frame_rate(unsigned frames_per_second) {
    installed_hooks.frame_rate = frames_per_second;
    if (settings_window != nil) {
        [settings_window showFrameRate:frames_per_second];
    }
}

}  // namespace minigolf::platform
