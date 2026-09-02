//! Run an iPod game in a window.
//!
//!   play <game.bin> [--gamedir=DIR] [--scale=N] [--fps=N]
//!
//! Everything the offline `trace` tool established is wired up here: context arguments to the
//! frame vectors, the manifest texture pre-load, the allocator, the clock, and file I/O. The
//! difference is only that frames go to a window instead of a `.ppm`.

use std::env;
use std::fs;
use std::path::PathBuf;
use std::time::{Duration, Instant};

use arm7tdmi::Bus;
use eapp_loader::{EApp, Machine, Stop, Stub, FB_HEIGHT, FB_WIDTH};
use minifb::{Key, MouseButton, Scale, ScaleMode, Window, WindowOptions};

/// Post one button event, the way RetailOS does.
///
/// `0x0024d918` is Apple's `postEvent(mgr, type, state, payload)`: it allocates a **12-byte node**,
/// writes `type` at `+0x00`, `state` at `+0x01`, `payload` at `+0x04` and `next` at `+0x08`, then
/// appends it to a linked list whose head is `mgr+0x26c` and tail `mgr+0x270`. The frame pump at
/// `0x0024dad4` copies that head into `ctx+0x30`, and the game hands it straight to its input
/// dispatcher — so a button is a node on this list, not a byte in a field.
///
/// The struct matches Apple's `InputEvents #1` decoder at `0x0026a8bc`, which reads `[r0+0]` as
/// the type and does nothing unless `[r0+1]` is 1.
/// The game's input flags word, `[r9+0x14]` where r9 is the literal at `0x18018b48`.
///
/// Bits 0..4 are the five click-wheel buttons. The poll at `0x18018a20` does `bic r0,r0,#0x60`,
/// clearing only the wheel bits, so a button bit set here survives into the frame's dispatch.
/// **Minigolf's** address. Every title puts this word somewhere different, so it is only used
/// for a binary known to be Minigolf; `--flags-addr=` overrides it for anything else. Poking it
/// blindly in another game would write into whatever that game keeps at the same offset, which is
/// inside its own image — a corruption that would look like a rendering or logic bug, not input.
const MINIGOLF_FLAGS: u32 = 0x1803_7a0c;
/// Select, Menu, Play/Pause, Next, Previous — the bits `0x18008304` onward tests one by one.
const BTN_SELECT: u32 = 0x01;
const BTN_MENU: u32 = 0x02;
const BTN_PLAY: u32 = 0x04;
const BTN_NEXT: u32 = 0x08;
const BTN_PREV: u32 = 0x10;

/// Press a button by setting its bit, and post a wheel sample so the frame dispatches.
///
/// With no known flags word the button half is skipped and only the wheel sample is queued —
/// scrolling still works, and nothing is written to an address we cannot vouch for.
fn press_button(m: &mut Machine, flags: Option<u32>, bit: u32, wheel: u8) {
    if let Some(addr) = flags {
        let cur = m.mem.read32(addr);
        m.mem.poke32(addr, cur | bit);
    }
    m.queue_input(wheel);
}

fn post_event(m: &mut Machine, ctx_base: u32, node: u32, ty: u8, state: u8, payload: u32, wheel: u8) {
    // The real mechanism, from Apple's `postEvent` at `0x0024d918`: a 12-byte node —
    // `{ type at +0x00, state at +0x01, payload at +0x04, next at +0x08 }` — appended to a list
    // whose head is `mgr+0x26c`. The frame pump at `0x0024dad4` republishes that head into
    // `ctx+0x30`, and the game hands it straight to its input dispatcher. The same struct is what
    // Apple's `InputEvents #1` decoder at `0x0026a8bc` reads, and it ignores any event whose
    // state byte is not 1.
    //
    // This is the route that selected a letter. `ctx+0x100` is NOT it — that is the frame
    // callback's reason code, and any non-zero value there sends the game down a lifecycle path
    // (which is what "every key resets the game" was).
    m.mem.poke8(node, ty);
    m.mem.poke8(node + 1, state);
    m.mem.poke32(node + 4, payload);
    m.mem.poke32(node + 8, 0); // next — a list of one
    m.mem.poke32(ctx_base + 0x30, node);
    // The game only LOOKS at the event list when its input flags are non-zero:
    //
    //   18018a44  ldr r0,[r9,#0x14] / cmp r0,#0 / beq   -> skip the dispatcher
    //   18018a50  ldr r1,[r4,#0x30] / bl 0x18011528     -> dispatch
    //
    // and those flags are only set by an `InputEvents #0` poll that reports an event. So a button
    // pressed while the wheel is still is never read at all — which is why Select appeared to
    // work (you are usually scrolling) and every other button appeared dead. Post a wheel sample
    // alongside the button so the frame carries one.
    m.queue_input(wheel);
}


/// Ask the window server to constrain the window to the panel's aspect ratio.
///
/// `ScaleMode::AspectRatioStretch` keeps the *image* undistorted, but it does that by letterboxing
/// inside whatever rectangle the drag produced — the window itself still takes any shape. This
/// locks the shape instead, so a drag from any corner can only produce a 4:3 window and there is
/// no black bar to letterbox into.
///
/// macOS does this natively: `-[NSWindow setContentAspectRatio:]` makes the window server itself
/// constrain the live resize. minifb's `get_window_handle` returns the `NSWindow*` (its
/// `mfb_open` returns an `OSXWindow`, an `NSWindow` subclass), so the whole thing is one message
/// send and needs no new dependency. `setContentAspectRatio:` rather than `setAspectRatio:`
/// because the ratio we care about is the panel's, not the panel plus the title bar's.
///
/// Elsewhere this is a no-op: minifb exposes no portable size constraint, and the letterboxing
/// fallback above still keeps the picture correct.
#[cfg(target_os = "macos")]
fn lock_aspect_ratio(window: &Window) {
    use std::ffi::{c_char, c_void, CString};

    #[repr(C)]
    struct NSSize {
        width: f64,
        height: f64,
    }

    #[link(name = "objc")]
    extern "C" {
        fn sel_registerName(name: *const c_char) -> *mut c_void;
        fn objc_msgSend();
    }

    let handle = window.get_window_handle();
    if handle.is_null() {
        return;
    }
    let Ok(name) = CString::new("setContentAspectRatio:") else { return };
    // SAFETY: `handle` is the NSWindow minifb created and still owns, and the selector takes a
    // single NSSize by value. `objc_msgSend` has no single Rust-expressible signature, so it is
    // transmuted to the one this call actually uses — the documented way to send a message
    // without an Objective-C binding crate.
    unsafe {
        let sel = sel_registerName(name.as_ptr());
        if sel.is_null() {
            return;
        }
        let send: extern "C" fn(*mut c_void, *mut c_void, NSSize) =
            std::mem::transmute(objc_msgSend as *const ());
        send(
            handle,
            sel,
            NSSize {
                width: FB_WIDTH as f64,
                height: FB_HEIGHT as f64,
            },
        );

        // Read it back. A message send to a class that does not respond is silent, so without
        // this the difference between "locked" and "did nothing" would only show up as the user
        // dragging the window into a shape it should not take.
        let Ok(getter) = CString::new("contentAspectRatio") else { return };
        let get_sel = sel_registerName(getter.as_ptr());
        if get_sel.is_null() {
            return;
        }
        // NSSize is two doubles, i.e. a homogeneous float aggregate, so it comes back in the
        // floating-point return registers and the ordinary `objc_msgSend` is the right entry.
        let get: extern "C" fn(*mut c_void, *mut c_void) -> NSSize =
            std::mem::transmute(objc_msgSend as *const ());
        let got = get(handle, get_sel);
        if got.width * FB_HEIGHT as f64 != got.height * FB_WIDTH as f64 {
            println!(
                "warning: window aspect not locked (window server reports {}x{})",
                got.width, got.height
            );
        }
    }
}

#[cfg(not(target_os = "macos"))]
fn lock_aspect_ratio(_window: &Window) {}


/// Every `afplay` this process has started, so it can be stopped however the emulator dies.
///
/// Returning from `main` is only one of the ways out, and it is not the common one: minifb's
/// standard macOS menu wires Quit to `-[NSApplication terminate:]`, which calls `exit()` without
/// unwinding, and Ctrl-C or `kill` do not unwind either. Cleanup written at the end of `main`
/// therefore covers the *least* likely exit, which is why the music kept playing after quitting.
///
/// The registry is a flat array of atomics rather than a `Vec` behind a lock because a signal
/// handler runs it: `kill(2)` is async-signal-safe, taking a mutex is not. Sixteen slots is well
/// past the four-voice pool plus one music track.
mod reaper {
    use std::sync::atomic::{AtomicI32, Ordering};

    const SLOTS: usize = 16;
    #[allow(clippy::declare_interior_mutable_const)]
    const EMPTY: AtomicI32 = AtomicI32::new(0);
    static PIDS: [AtomicI32; SLOTS] = [EMPTY; SLOTS];

    // From libSystem, which is always linked. Declared here rather than taking a `libc`
    // dependency for four symbols.
    extern "C" {
        fn atexit(cb: extern "C" fn()) -> i32;
        fn signal(sig: i32, handler: usize) -> usize;
        fn kill(pid: i32, sig: i32) -> i32;
    }
    const SIGKILL: i32 = 9;
    const SIGINT: i32 = 2;
    const SIGTERM: i32 = 15;
    const SIGHUP: i32 = 1;

    pub fn track(pid: u32) {
        for slot in PIDS.iter() {
            if slot.compare_exchange(0, pid as i32, Ordering::SeqCst, Ordering::SeqCst).is_ok() {
                return;
            }
        }
    }

    pub fn forget(pid: u32) {
        for slot in PIDS.iter() {
            let _ = slot.compare_exchange(pid as i32, 0, Ordering::SeqCst, Ordering::SeqCst);
        }
    }

    /// Kill everything still registered. Safe to run more than once — a slot is cleared as it is
    /// taken, and killing an already-dead pid just fails.
    pub extern "C" fn reap_all() {
        for slot in PIDS.iter() {
            let pid = slot.swap(0, Ordering::SeqCst);
            if pid > 0 {
                // SAFETY: `pid` is a child this process spawned and has not reaped.
                unsafe { kill(pid, SIGKILL) };
            }
        }
    }

    extern "C" fn on_signal(sig: i32) {
        reap_all();
        std::process::exit(128 + sig);
    }

    /// Arrange for `reap_all` to run on a normal `exit()` and on the usual termination signals.
    ///
    /// A `SIGKILL` of the emulator itself cannot be caught, so that one case still orphans the
    /// players — there is no mechanism on macOS that would cover it.
    pub fn install() {
        // SAFETY: registering handlers before any child exists.
        unsafe {
            atexit(reap_all);
            for sig in [SIGINT, SIGTERM, SIGHUP] {
                signal(sig, on_signal as usize);
            }
        }
    }
}

/// Start a sound and register it with the reaper.
fn play_file(path: &std::path::Path) -> Option<std::process::Child> {
    match std::process::Command::new("afplay").arg(path).spawn() {
        Ok(child) => {
            reaper::track(child.id());
            Some(child)
        }
        Err(e) => {
            println!("  (cannot play {}: {e})", path.display());
            None
        }
    }
}

/// Stop a sound and deregister it.
fn stop_child(child: &mut std::process::Child) {
    reaper::forget(child.id());
    let _ = child.kill();
    let _ = child.wait();
}


/// The event-node type that produces a given button bit, from Bejeweled's decoder at 0x18013ebc.
fn event_type_for(bit: u32) -> u8 {
    match bit {
        BTN_SELECT => 2,
        BTN_MENU => 3,
        BTN_PLAY => 4,
        BTN_NEXT => 5,
        _ => 1, // BTN_PREV, the 0x10 bit
    }
}

const RAM_BASE: u32 = 0x1100_0000;
const RAM_SIZE: usize = 0x0080_0000;

fn main() {
    reaper::install();
    let args: Vec<String> = env::args().skip(1).collect();
    let Some(path) = args.iter().find(|a| !a.starts_with("--")) else {
        eprintln!("usage: play <game.bin> [--gamedir=DIR] [--scale=N] [--fps=N] [--budget=N]\n         [--async-files] [--wheel-sensitivity=N] [--wheel-invert]");
        std::process::exit(2);
    };
    let opt = |k: &str, d: usize| -> usize {
        args.iter()
            .find_map(|a| a.strip_prefix(k))
            .and_then(|v| v.parse().ok())
            .unwrap_or(d)
    };

    let image = fs::read(path).unwrap_or_else(|e| {
        eprintln!("{path}: {e}");
        std::process::exit(1);
    });
    let app = EApp::parse(image).unwrap_or_else(|e| {
        eprintln!("not loadable: {e:?}");
        std::process::exit(1);
    });

    let mut m = Machine::new(&app, RAM_BASE, RAM_SIZE);

    // --callgraph-dump=FILE : every branch edge actually taken during the session, written at
    // exit as `site target count` lines. `trace` has had this for a while, but `trace` cannot
    // drive a title past its first frame — only this binary has the frame pump, the synthetic
    // context and the scripted wheel. The set of edge TARGETS is the set of function entry
    // points a real session reaches, which is the measurement a decompilation plan needs:
    // how much of the binary is live code, and which functions a port can leave until last.
    let callgraph_dump: Option<String> = args
        .iter()
        .find_map(|a| a.strip_prefix("--callgraph-dump="))
        .map(str::to_string);
    if callgraph_dump.is_some() {
        m.edges = Some(Default::default());
        println!("callgraph: recording every branch edge taken");
    }

    // --open-returns-handle : make an open report success by returning the handle rather than 0.
    // Off by default, because the two conventions are opposites and every title measured before
    // Minigolf wanted the zero. See `Stub::FileOpen`.
    let open_ret_handle = args.iter().any(|a| a == "--open-returns-handle");
    if open_ret_handle {
        println!("open-returns-handle: FileOpen returns the handle (0 = miss = failure)");
    }

    // Identified framework entry points — see README §"The GL surface actually in use".
    m.set_stub("miscTBD", 0, Stub::Alloc);
    m.set_stub("miscTBD", 1, Stub::Free { arg: 0 });
    m.set_stub("miscTBD", 9, Stub::Clock { arg: 0, step: 16_667 });
    // The iPod status bar the game draws itself: #12 is the time of day, #13 the battery level.
    // Both were unstubbed, so the clock formatted whatever was on the stack and the gauge read
    // empty. See `Stub::HostTime` and `Stub::HostBattery` for the disassembly behind each.
    // Metadata: Lost reaches exactly two ordinals, both from one wrapper at 0x18006d48.
    // #62 is the now-playing playlist — it is never dereferenced, only handed back, so a stable
    // non-NULL block is enough; a NULL would be the failure value. #134 is its track count.
    // The render-server lifecycle — NOT a draw path. Lost draws with the ordinary
    // #137/#40/#37 vertex-array calls, but only once the server reports itself started.
    // #152 start, #153 stop, #159 select built-in pipeline, #164 set the server image
    // (the `rserver.bin` blob). Each answers 1 for success; 0, which an unstubbed entry
    // returns, means failure.
    m.set_stub("OpenGLES", 125, Stub::GlUniformMatrix { value: 3 });
    m.set_stub("OpenGLES", 152, Stub::GlStartRenderServer);
    m.set_stub("OpenGLES", 153, Stub::Value(1));
    m.set_stub("OpenGLES", 159, Stub::Value(1));
    m.set_stub("OpenGLES", 164, Stub::Value(1));

    let playlist = m.scratch(0x90);
    m.set_stub("Metadata", 62, Stub::Value(playlist));
    m.set_stub("Metadata", 134, Stub::AudioStreamCount);
    m.set_stub("miscTBD", 12, Stub::HostTime { out: 0 });
    m.set_stub("miscTBD", 13, Stub::HostBattery);
    // Everything the §18.0 coverage audit settled, shared with `trace` so a finding cannot be
    // true in the viewer and missing from the tool that measures it.
    m.install_audit_stubs();
    // --battery=N reports N% instead of this machine's charge, for looking at the gauge at a
    // level the host does not happen to be at.
    m.battery_override = args
        .iter()
        .find_map(|a| a.strip_prefix("--battery="))
        .and_then(|n| n.parse::<u8>().ok())
        .map(|n| n.min(100));
    m.set_stub("OpenGLES", 12, Stub::GlClear);
    m.set_stub("OpenGLES", 13, Stub::GlClearColor);
    m.set_stub("OpenGLES", 157, Stub::GlSwap);
    m.set_stub("OpenGLES", 137, Stub::GlVertexAttribPointer);
    m.set_stub("OpenGLES", 37, Stub::GlDrawArrays);
    // #38 glDrawElements — Pac-Man's maze and pellet field are indexed draws.
    m.set_stub("OpenGLES", 38, Stub::GlDrawElements);
    // #45 glGenTextures — names start at 1, so 0 stays "unbound".
    m.set_stub("OpenGLES", 45, Stub::GlGenTextures);
    // #148 glUniform4xvAPPLE — the per-draw modulate colour, 16.16 fixed. #120 is the float twin.
    m.set_stub("OpenGLES", 148, Stub::GlUniform4x { fixed: true });
    m.set_stub("OpenGLES", 120, Stub::GlUniform4x { fixed: false });
    // #158 — a private enable/disable whose meaning lives in the render-server firmware. Accept it.
    m.set_stub("OpenGLES", 158, Stub::Value(0x3000));
    // #165/#166 loadIdentity and #167 ortho are plain matrix maths with no driver state.
    m.set_stub("OpenGLES", 165, Stub::GlLoadIdentity { fixed: false });
    m.set_stub("OpenGLES", 166, Stub::GlLoadIdentity { fixed: true });
    m.set_stub("OpenGLES", 167, Stub::GlOrtho);
    // The mat4 helpers. #175 is not optional: Minigolf's only glUniformMatrix4fv upload is built
    // by it into a stack frame, so leaving it a no-op fed that upload uninitialised stack.
    m.set_stub("OpenGLES", 169, Stub::GlMatrixOp { op: eapp_loader::MatrixOp::Translate });
    m.set_stub("OpenGLES", 171, Stub::GlMatrixOp { op: eapp_loader::MatrixOp::Scale });
    m.set_stub("OpenGLES", 173, Stub::GlMatrixOp { op: eapp_loader::MatrixOp::Rotate });
    m.set_stub("OpenGLES", 175, Stub::GlMatrixOp { op: eapp_loader::MatrixOp::Mult });
    // #105 glTexSubImage2D — Bejeweled and Zuma refill existing textures through it.
    m.set_stub("OpenGLES", 105, Stub::GlTexSubImage2D);
    // Lost's own colour and matrix paths: it calls neither #148 nor #125.
    m.set_stub("OpenGLES", 147, Stub::GlUniform4xScalar);
    m.set_stub("OpenGLES", 149, Stub::GlUniformMatrixFixed);
    // #53 glGetError is exactly `return the pending error, then clear it`; 0 is the right answer
    // when none is pending. #84 glPixelStorei is inert on this driver — neither #99 nor #105
    // consults alignment — and #101 glTexParameterf stores nothing, so filters are fixed-function.
    m.set_stub("OpenGLES", 4, Stub::GlBindTexture);
    m.set_stub("OpenGLES", 19, Stub::GlCompressedTexImage2D);
    // #99 glTexImage2D — named from Apple's implementation at 0x00270240. Unstubbed it returned
    // 0 and the upload was dropped, which is why the golf course rendered as a white field.
    m.set_stub("OpenGLES", 99, Stub::GlTexImage2D);
    // #21 glCopyTexImage2D — render-to-texture. Minigolf allocates a screen-sized texture from a
    // placeholder and then fills it from the framebuffer; without this the placeholder is drawn.
    m.set_stub("OpenGLES", 21, Stub::GlCopyTexImage2D);
    m.set_stub("OpenGLES", 40, Stub::GlEnableVertexAttribArray);
    m.set_stub("OpenGLES", 36, Stub::GlDisableVertexAttribArray);
    // MEASURED: Apple's implementation at 0x00268890 is literally `mov r0,#0xff; bx lr` — it
    // returns 255. research/01 identified it as "a divisor, any non-zero avoids a divide-by-zero"
    // by sweeping constants, which is true but landed on 1; this is the actual value.
    m.set_stub("Audio", 52, Stub::Value(0xff));
    // The audio stream model, measured: miscTBD #14 resolves a name, Audio #40 registers it,
    // Audio #43 plays one by index.
    m.set_stub("miscTBD", 14, Stub::ResolveName { name: 3, out: 1 });
    m.set_stub("Audio", 0, Stub::AudioSfxRegister { idx: 1 });
    m.set_stub("Audio", 40, Stub::AudioRegister);
    m.set_stub("Audio", 43, Stub::AudioPlay { arg: 0 });
    // #48 carries the player's repeat setting; the game sets it to 1 before starting the music
    // and then never issues another play, so the loop is the player's job, not the game's.
    m.set_stub("Audio", 48, Stub::AudioRepeat { arg: 0 });
    // The sound-effect path, from Apple's implementations: #0 creates a descriptor and returns
    // its slot index, #7 points that descriptor at the PCM, and #2 plays it. #8 is a buffer-LENGTH
    // setter and was never the trigger, which is why hooking it captured nothing across a whole
    // hole of play.
    m.set_stub("Audio", 7, Stub::SfxSetBuffer { handle: 0, ptr: 1 });
    m.set_stub("Audio", 2, Stub::SfxPlay { handle: 0 });
    m.set_stub("Audio", 16, Stub::SfxRepeat { handle: 0, count: 1 });
    // `poll(&out0, &out1)`. Apple's implementation at `0x001181f8` writes a delta to out0 and the
    // encoded event word to out1; this fills out1, which is the one Minigolf reads (the only stack
    // reads in its whole handler are `[sp+4]`).
    m.set_stub("InputEvents", 0, Stub::InputPoll { arg: 0, offset: 4 });
    m.set_stub("Filesytem", 0, Stub::FileOpen { path: 1, out: 3, return_handle: open_ret_handle });
    m.set_stub("AsyncFileIO", 0, Stub::FileOpen { path: 1, out: 3, return_handle: open_ret_handle });
    m.set_stub("AsyncFileIO", 3, Stub::FileOpen { path: 1, out: 2, return_handle: open_ret_handle });
    let rd = Stub::FileRead { handle: 0, buffer: 1, length: 2, out: 3 };
    m.set_stub("Filesytem", 2, rd.clone());
    m.set_stub("AsyncFileIO", 2, rd);

    // --async-files : model AsyncFileIO as RetailOS implements it — accept the operation, park
    // the request, and run the game's completion callback between frames. Request-object
    // register per import, from the shims at 0x002680e4 / 0x00268118 / 0x00268144.
    let async_files = args.iter().any(|a| a == "--async-files");
    if async_files {
        println!("async-files: AsyncFileIO #0/#3 open, #2 read, completions drained per frame");
        m.set_stub("AsyncFileIO", 0, Stub::AsyncOpen { path: 1, request: 3 });
        m.set_stub("AsyncFileIO", 3, Stub::AsyncOpen { path: 1, request: 2 });
        m.set_stub("AsyncFileIO", 2, Stub::AsyncRead { request: 0 });
        // #1 takes the request in r0, #4 in r2 (shims at 0x002680c8 / 0x00268160). Leaving #1
        // unstubbed returns 0, which the game reads as failure one step after the open.
        m.set_stub("AsyncFileIO", 1, Stub::AsyncOp { request: 0 });
        m.set_stub("AsyncFileIO", 4, Stub::AsyncOp { request: 2 });
        // #12/#14/#16 are the save/settings store — they route through a different singleton
        // (0x0017154c) than the file entries and only appear when the pause menu opens. Left
        // unstubbed they return 0, i.e. "failed", and the menu stalls before drawing its items.
        // Reporting success is a guess at the value but a well-founded one about the direction.
        m.set_stub("AsyncFileIO", 12, Stub::Value(1));
        m.set_stub("AsyncFileIO", 14, Stub::Value(1));
        m.set_stub("AsyncFileIO", 16, Stub::Value(1));
    }

    // Resources default to the directory two levels above the executable — the layout every
    // title ships as `<Game>/Executables/<name>.bin`.
    m.game_dir = args
        .iter()
        .find_map(|a| a.strip_prefix("--gamedir="))
        .map(PathBuf::from)
        .or_else(|| PathBuf::from(path).parent()?.parent().map(|p| p.to_path_buf()));
    if let Some(d) = &m.game_dir {
        println!("resources: {}", d.display());
    }
    for l in m.preload_textures() {
        println!("  {l}");
    }

    // The game's timers should follow the player's clock, not our call rate.
    m.wall_clock = !args.iter().any(|a| a == "--fixed-clock");
    // --time=HH:MM pins the time of day the game shows (the title's clock), so a recording does
    // not depend on the hour it was made at.
    m.fixed_time = args.iter().find_map(|a| a.strip_prefix("--time=")).map(|v| {
        let (h, mi) = v.split_once(':').unwrap_or_else(|| panic!("--time wants HH:MM, not {v}"));
        (h.parse().expect("--time hour"), mi.parse().expect("--time minute"))
    });
    // --load-on-open: an async open whose request carries a buffer loads the whole file.
    m.load_on_open = args.iter().any(|a| a == "--load-on-open");
    // --allow-creates: a write-mode open (mode 1) creates the file if it is missing.
    m.allow_creates = args.iter().any(|a| a == "--allow-creates");
    // --no-rewind: a load-on-open continues from the previous position instead of restarting.
    m.rewind_after_load = !args.iter().any(|a| a == "--no-rewind");
    // --flip-y: the game hands vertices in top-left screen coordinates, so the rasteriser must
    // not apply its own GL bottom-left flip. Bejeweled needs this; its projection matrix is the
    // identity, so there is nothing in-band to derive it from.
    m.proj_flips_y = args.iter().any(|a| a == "--flip-y");

    // Novelty tracking: which code buckets the game has entered, and when. When it goes idle the
    // most recently *new* code is what it reached last before deciding to wait — that names the
    // stall without guessing at framework return values.
    m.novelty = Some(Default::default());
    m.arm_novelty();
    // --watch-pc=ADDR[,ADDR…] : record arrivals at game addresses with their registers. The
    // sound-effect gate at 0x18017d6c (`ldr r0,[r4,#0x124] / cmp r0,r2 / bne`) can only be caught
    // while playing, so it has to be armed here rather than in the headless tool.
    // --watch-mem=ADDR : report every instruction that changes one 32-bit word.
    if let Some(a) = args
        .iter()
        .find_map(|a| a.strip_prefix("--watch-mem="))
        .and_then(|v| u32::from_str_radix(v.trim().trim_start_matches("0x"), 16).ok())
    {
        m.watch = Some(a);
        println!("watching memory {a:#010x}");
    }
    for spec in args.iter().filter_map(|a| a.strip_prefix("--watch-pc=")) {
        for t in spec.split(',') {
            if let Ok(pc) = u32::from_str_radix(t.trim().trim_start_matches("0x"), 16) {
                m.enter_pcs.push(pc);
                m.enter_bloom |= 1u64 << ((pc >> 2) & 63);
                println!("watching pc {pc:#010x}");
            }
        }
    }
    m.started = Some(Instant::now());
    let budget = opt("--budget=", 8_000_000);
    // The context RetailOS actually passes, from `0x0024da80` — the eApp task's frame pump:
    //
    //   0024dafc  add r1, r4, #0x100   ; second argument
    //   0024db00  mov r0, r4           ; first argument = the eApp manager itself
    //   0024db08  bx  r5               ; r5 = [r4+0x260], the frame vector
    //
    // So the two arguments are ONE object and a pointer 0x100 into it, not two independent
    // buffers. Passing separate scratch broke every field the game reaches through both.
    // The pump also fills three fields immediately before the call:
    //   [ctx+0x00] = 5 or 4, a state byte
    //   [ctx+0x2c] = a query on the AsyncFileIO subsystem (0x001e3c14)
    //   [ctx+0x30] = [manager+0x26c]
    let ctx_base = m.scratch(0x400);
    m.mem.poke8(ctx_base, 5);
    let ctx = vec![ctx_base, ctx_base + 0x100, 0, 0];

    // Init vectors run once; the last non-zero vector is the per-frame callback.
    let mut frame_vector = None;
    for (i, &v) in app.vectors.iter().enumerate() {
        if v == 0 {
            continue;
        }
        let stop = m.call_with(v, &ctx, budget);
        println!("vector[{i}] {v:#010x} -> {stop:?}");
        frame_vector = Some(v);
    }
    let Some(frame_vector) = frame_vector else {
        eprintln!("no entry vector");
        std::process::exit(1);
    };
    // --call-log=FILE writes every framework call as one line, `FRAME Framework#ord r0 r1 r2 r3
    // sp0 sp1 sp2 sp3 from LR`, so a static recompilation of the same title can be diffed against
    // this emulator call for call (recomps/Mini Golf/tests/diff.sh). Calls made by the init
    // vectors are frame 0, as are the first frame's.
    let mut call_log = args
        .iter()
        .find_map(|a| a.strip_prefix("--call-log="))
        .map(|path| {
            let file = fs::File::create(path).unwrap_or_else(|e| panic!("--call-log={path}: {e}"));
            println!("call log -> {path}");
            (std::io::BufWriter::new(file), 0usize)
        });
    fn flush_call_log(log: &mut Option<(std::io::BufWriter<fs::File>, usize)>, trace: &[eapp_loader::Call], frame: usize) {
        use std::io::Write;
        if let Some((out, flushed)) = log {
            for c in &trace[*flushed..] {
                let _ = writeln!(
                    out,
                    "{frame} {}#{} {:08x} {:08x} {:08x} {:08x} {:08x} {:08x} {:08x} {:08x} from {:08x}",
                    c.framework, c.index, c.args[0], c.args[1], c.args[2], c.args[3],
                    c.stack[0], c.stack[1], c.stack[2], c.stack[3], c.return_to
                );
            }
            *flushed = trace.len();
        }
    }
    flush_call_log(&mut call_log, &m.trace, 0);
    // --enter-log=FILE writes every arrival at a --watch-pc address as `enter PC <-LR r0-7:...`,
    // the format the recomp's --trace-entry uses, so the two can be diffed.
    let mut enter_log = args
        .iter()
        .find_map(|a| a.strip_prefix("--enter-log="))
        .map(|path| std::io::BufWriter::new(fs::File::create(path).unwrap_or_else(|e| panic!("--enter-log={path}: {e}"))));
    m.enter_trace_on = enter_log.is_some();
    fn flush_enter_log(log: &mut Option<std::io::BufWriter<fs::File>>, m: &mut Machine) {
        use std::io::Write;
        if let Some(out) = log {
            for (pc, lr, r) in m.enter_trace.drain(..) {
                let _ = writeln!(out, "enter {pc:#010x} <-{lr:#010x} r0-7:{:08x} {:08x} {:08x} {:08x} {:08x} {:08x} {:08x} {:08x}", r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
            }
        } else {
            m.enter_trace.clear();
        }
    }
    flush_enter_log(&mut enter_log, &mut m);

    let scale = match opt("--scale=", 3) {
        1 => Scale::X1,
        2 => Scale::X2,
        4 => Scale::X4,
        8 => Scale::X8,
        _ => Scale::X4,
    };
    // The game's real name, from its manifest. The directory it lives in is named by an opaque
    // id — 50513, 88888, 1500C — which is what a window titled from the path used to show.
    // Falls back to the executable's own name with the version and build stripped
    // ("Sudoku_1_1_2703081" -> "Sudoku"), and only then to the directory id.
    let exe_stem = PathBuf::from(path)
        .file_stem()
        .map(|n| n.to_string_lossy().into_owned())
        .unwrap_or_default();
    let from_exe = exe_stem.split("_1_1_").next().unwrap_or(&exe_stem).to_string();
    let title = m
        .game_dir
        .as_deref()
        .and_then(eapp_loader::manifest_name)
        .or_else(|| (!from_exe.is_empty()).then_some(from_exe))
        .unwrap_or_else(|| {
            PathBuf::from(path)
                .parent()
                .and_then(|p| p.parent())
                .and_then(|p| p.file_name())
                .map(|n| n.to_string_lossy().into_owned())
                .unwrap_or_else(|| "iPod".into())
        });

    println!("title: {title}");

    let mut window = Window::new(
        &format!("{title} — iPod 5G"),
        FB_WIDTH,
        FB_HEIGHT,
        WindowOptions {
            scale,
            // Draggable from any edge, but the panel keeps the iPod's 4:3 shape: the spare space
            // is let out to the background rather than stretching a 320x240 screen into whatever
            // rectangle the drag happens to make.
            resize: true,
            scale_mode: ScaleMode::AspectRatioStretch,
            ..WindowOptions::default()
        },
    )
    .unwrap_or_else(|e| {
        eprintln!("cannot open window: {e}");
        std::process::exit(1);
    });

    lock_aspect_ratio(&window);

    // `--fps=0` removes the limiter and lets the emulator run as fast as it can. That is safe
    // for the game's own timekeeping because `wall_clock` is on by default: `miscTBD #9` answers
    // with real elapsed microseconds rather than a fixed step per call, so the game's timers run
    // at the player's speed no matter how many frames get drawn between them. With a fixed step
    // (`--fixed-clock`) an uncapped run really would fast-forward the game.
    let fps = opt("--fps=", 60);
    let target = Duration::from_micros(if fps == 0 { 0 } else { 1_000_000 / fps as u64 });
    // 60 by default: `miscTBD #9` advances the clock 16_667 us per call, so the game's own
    // timebase is 60 Hz and anything slower makes it run in slow motion against its own reckoning.
    // 0 disables minifb's own pacing.
    window.set_target_fps(fps);
    if fps == 0 {
        println!("frame limiter off — running as fast as the host allows");
        if !m.wall_clock {
            println!("  warning: --fixed-clock is on, so the game's timers will run fast too");
        }
    }

    let mut buf = vec![0u32; FB_WIDTH * FB_HEIGHT];
    let mut frames = 0usize;
    let started = Instant::now();
    let mut last_report = Instant::now();
    let mut last_log = Instant::now();

    // Number keys 1-9 post EVENT TYPE 1..9 into ctx+0x100 — the field that actually carries
    // buttons. Confirmed 2026-08-19: type 1 navigates back to the title screen, i.e. Menu.
    // The rest are unmapped; press each and watch what the game does.
    // Key1 is deliberately absent: type 1 quits the game, and having it one keypress from the
    // rest turned two play sessions into a gray screen that looked like a rendering fault.
    // Shift+Q posts it, below, for when quitting is actually wanted.

    // The click wheel, as an absolute position the way the hardware reports it.
    //
    // The byte in the event word is NOT a button id — Apple's encoder at `0x000e95a4` is
    // `bit30 | (((0x77 - raw) * 8 / 3) & 0xff)`, so it is a position on a **120-detent** wheel,
    // inverted on the way out. Modelling `raw` and running it through the same expression is what
    // makes the value the game sees indistinguishable from the hardware's.
    //
    // Scrolling up turns the wheel clockwise ("right"), scrolling down turns it counter-clockwise.
    // Because the transform inverts, clockwise is `raw` *decreasing*; if a title ever reads as
    // reversed, this sign is the one place to flip it.
    const WHEEL_DETENTS: i32 = 0x78; // 120
    // --wheel-sensitivity=N : scroll units per detent (smaller = faster). --wheel-invert flips
    // which way a swipe turns the wheel, because "clockwise" here is read off the transform's
    // inversion rather than measured against hardware.
    let scroll_per_detent: f32 = args
        .iter()
        .find_map(|a| a.strip_prefix("--wheel-sensitivity="))
        .and_then(|v| v.parse().ok())
        .unwrap_or(1.0f32)
        .max(0.05);
    let wheel_invert = args.iter().any(|a| a == "--wheel-invert");

    fn wheel_byte(raw: i32) -> u8 {
        ((((0x77 - raw) * 8) / 3) & 0xff) as u8
    }
    let mut wheel_raw: i32 = 0;
    let mut scroll_accum: f32 = 0.0;

    // The buttons — Space / left click = Select, W = Menu, A = Rewind, S = Play/Pause, D = Next.
    //
    // ⚠️ The byte these send is a GUESS, and the disassembly argues against it working: the game
    // funnels the low byte straight into `0x180135c8`, which does `mov r0, r0, lsl #16` and stores
    // it as a wheel position **unconditionally** — there is no branch on it that could distinguish
    // a button. So the real button state most likely arrives through the frame-vector context
    // (`[r4+0x30]`, the third word the UI reads every frame), which we still pass as zeroed
    // scratch.
    //
    // They are wired anyway because the values chosen are the ones a wheel position can never
    // take: the transform's image is exactly 96 of the 256 bytes (the iPod's 96-detent wheel), so
    // these five come from the 160-byte complement and are unambiguous if the game does read them.
    // `[` and `]` walk the Select candidate through that complement so it can be hunted live.
    // Event-type candidates. RetailOS's pump tests this byte against 5 and 6, so small
    // integers are the plausible space; 1..=12 covers it with room either side.
    const NON_WHEEL: [u8; 12] = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12];
    // Select is event type 2 — MEASURED: it commits a letter on the name-entry screen. `[` and
    // `]` still walk the candidate, for mapping the buttons that are not yet known.
    let mut select_idx: usize = 1;
    let mut select_byte: u8 = 2;
    let mut mouse_was_down = false;
    let mut event_hold: i32 = 0;
    let mut last_node: u32 = 0;
    let mut held_bits: u32 = 0;
    let mut shot_n: u32 = 1;
    // --frame-reason=auto state: whether we still own the byte, and what we last put there.
    let mut reason_ours = true;
    let mut reason_last: u8 = 0;
    // (The snapshot had this `let` below its first use; moved up so it compiles.)
    let pump_mark: Option<u8> = args
        .iter()
        .find_map(|a| a.strip_prefix("--pump-mark="))
        .and_then(|v| v.parse().ok());
    // Seed the dispatcher gate ONCE, not every frame. Held, it masks the game's own answer.
    if let Some(mk) = pump_mark {
        m.mem.poke8(ctx_base + 0x100, mk);
        println!("pump mark: ctx+0x100 seeded with {mk}");
    }
    // --frame-reason keeps the pump's reason byte refreshed each frame. `auto` runs the
    // handshake RetailOS actually runs; see `reason_auto` below.
    let reason_spec = args.iter().find_map(|a| a.strip_prefix("--frame-reason="));
    // `auto` or `auto:N` — N is the steady-state reason once the game has answered (default 1).
    let reason_auto = reason_spec.is_some_and(|v| v.starts_with("auto"));
    let reason_steady: u8 = reason_spec
        .and_then(|v| v.strip_prefix("auto:"))
        .and_then(|n| n.parse().ok())
        .unwrap_or(1);
    let per_frame_reason: Option<u8> = reason_spec.and_then(|v| v.parse().ok());
    // Which of the pump's two bytes this title treats as the reason. RetailOS writes `ctx+0x00`
    // and The Sims Bowling reads it there (`0x18045740: ldrb r0,[r5,#0]`), but Sudoku reads
    // `ctx+0x100` instead (`0x180311f4: ldrb r0,[r4,#0]`, r4 = r1 = ctx+0x100) and copies its own
    // state back into `ctx+0x00` at `0x180314b0`. The two titles use the pair in opposite roles,
    // so which one is driven has to be selectable until that is understood.
    let reason_off: u32 = args
        .iter()
        .find_map(|a| a.strip_prefix("--reason-offset="))
        .and_then(|v| u32::from_str_radix(v.trim().trim_start_matches("0x"), 16).ok())
        .unwrap_or(0);
    let answer_off: u32 = if reason_off == 0 { 0x100 } else { 0 };
    // `--pump-mark=N` writes N into `ctx+0x100` before every call.
    //
    // Sudoku gates its whole dispatcher on it: `0x18031258` does `cmp r0,#1 / bhi 0x18031330`
    // with r0 = `[ctx+0x100]`, and only past that branch does it read the reason from `ctx+0x00`
    // and jump through the table at `0x18031354` — where reason 0 is its one-time init. Left at
    // zero it takes the "nothing to do" arm every frame and never initialises at all.
    // The four-voice sound-effect pool, and how many triggers it turned away.
    let mut voices: Vec<(String, std::process::Child, Option<PathBuf>)> = Vec::new();
    let mut dropped_voices: u64 = 0;
    // The music. ONE track at a time, because the device has one player task: asking it to play
    // a second stream replaces the first rather than layering over it. Tracked as a process so it
    // can be stopped on exit, and so it can be restarted when it ends and repeat is set.
    struct Music {
        path: PathBuf,
        label: String,
        child: std::process::Child,
        repeat: bool,
    }
    let mut music: Option<Music> = None;
    // Somewhere to build the 12-byte event node.
    let event_node = m.scratch(0x10);
    // W is Menu, which is the one button whose type is known. The other three are still guesses
    // and post event types rather than event-word bytes, which is at least the right channel.
    // Type 1 is EXIT — measured: after it, quads stop and the frame vector drops from ~29 000
    // instructions to ~350, i.e. the game has torn down. It is deliberately not on a letter key.
    // Known: 1 = exit, 2 = Select. 3/4/5 are the other types Apple's `InputEvents #1` decoder
    // handles, so they are the candidates for Menu / Play-Pause / Next / Previous.
    // MEASURED, not inferred: bit 0x10 is the button that opens the pause menu, i.e. Menu.
    // The bit names below are otherwise still a guess at which physical button is which — the
    // disassembly proves there are five bits and where each is tested, not what they are called.
    // Which word carries the click-wheel button bits. Known only for Minigolf, where it was
    // measured; `--flags-addr=0x...` supplies it for any other title once someone finds it.
    let flags_addr: Option<u32> = args
        .iter()
        .find_map(|a| a.strip_prefix("--flags-addr="))
        .map(|v| {
            let v = v.trim_start_matches("0x");
            u32::from_str_radix(v, 16).unwrap_or_else(|e| panic!("--flags-addr: {e}"))
        })
        .or_else(|| {
            // The EXECUTABLE's name, not `title` — `title` is the grandparent directory, which
            // for these titles is a number like "88888", so matching on it never matched and
            // silently disabled the buttons for Minigolf too.
            let exe = PathBuf::from(path)
                .file_name()
                .map(|n| n.to_string_lossy().into_owned())
                .unwrap_or_default();
            exe.contains("Minigolf").then_some(MINIGOLF_FLAGS)
        })
        // Everything else: find it in the image. `find_flags_word` reproduces Minigolf's
        // hand-measured address from the signature alone, and gives one to nine further titles
        // that had no buttons at all — Bejeweled, Cubis 2, Mahjong, Ms. PAC-MAN's sibling
        // Pac-Man, Tetris, Texas Hold'em, TWA, Vortex and Zuma.
        .or_else(|| eapp_loader::find_flags_word(&app.image));
    // --event-buttons: deliver buttons as EVENT-LIST NODES rather than flag bits.
    //
    // Bejeweled decodes them at 0x18013ebc: it requires the node's state byte to be **2** (a
    // press; 1 is the release it handles separately) and switches on the type byte to reach the
    // same five bits Minigolf uses —
    //   type 1 -> 0x10   type 2 -> 0x01   type 3 -> 0x02   type 4 -> 0x04   type 5 -> 0x08
    // so Select is type 2. Minigolf reads flag bits instead, which is why one mechanism does not
    // serve both.
    let event_buttons = args.iter().any(|a| a == "--event-buttons");
    if event_buttons {
        println!("buttons delivered as event-list nodes (state 2)");
    }

    match flags_addr {
        Some(a) => println!("button flags word at {a:#010x}"),
        None => println!(
            "no button flags word known for this title — wheel works, buttons do nothing \
             (pass --flags-addr=0x... once it is found)"
        ),
    }

    let buttons: &[(Key, &str, u32)] = &[
        (Key::W, "Menu", BTN_PREV),
        (Key::A, "btn-0x02", BTN_MENU),
        (Key::S, "Play/Pause", BTN_PLAY),
        (Key::D, "Next", BTN_NEXT),
    ];

    // --script=FILE drives the game from a file instead of the keyboard, so a session that
    // reaches a course is reproducible. Every question still open — where the pause menu stalls,
    // why no save is ever written, what triggers a sound effect — needs the game driven several
    // menus deep, and doing that by hand makes each answer a one-off that cannot be re-measured.
    //
    // One `FRAME: ACTION` per line, `#` comments, blank lines ignored:
    //   120: select | menu | play | next | prev     one button press
    //   140: wheel +6                               six detents right (negative for left)
    //   200: shot                                   write the framebuffer to /tmp/ipod-shot-NN.png
    //   900: quit                                   stop, so a run ends on its own
    //   900: terminate                              exit() without unwinding, as Cmd-Q does
    let script: Vec<(usize, String)> = args
        .iter()
        .find_map(|a| a.strip_prefix("--script="))
        .map(|path| {
            let text = std::fs::read_to_string(path)
                .unwrap_or_else(|e| panic!("--script={path}: {e}"));
            let mut steps: Vec<(usize, String)> = text
                .lines()
                .map(|l| l.split('#').next().unwrap_or("").trim())
                .filter(|l| !l.is_empty())
                .map(|l| {
                    let (f, act) = l.split_once(':').unwrap_or_else(|| {
                        panic!("--script: expected 'FRAME: ACTION', got {l:?}")
                    });
                    let f = f.trim().parse().unwrap_or_else(|e| {
                        panic!("--script: bad frame number {f:?}: {e}")
                    });
                    (f, act.trim().to_lowercase())
                })
                .collect();
            // Sorted so the file may be written in any order, and so the cursor below can walk it.
            steps.sort_by_key(|&(f, _)| f);
            println!("script: {} step(s) from {path}", steps.len());
            steps
        })
        .unwrap_or_default();
    let mut script_at = 0usize;
    let mut script_quit = false;

    // A scripted run takes no live input: the keyboard and the mouse wheel would make the
    // recording depend on whatever the host was doing at the time.
    let live = script.is_empty();
    while window.is_open() && !(live && window.is_key_down(Key::Escape)) && !script_quit {
        // Retire last frame's buttons FIRST, so a press set below survives into `call_with`.
        // Clearing after the press instead cleared it in the same frame and the game never saw
        // it — which is how W/A/S/D went dead while Select, handled after the clear, still worked.
        // One frame with the bit set is one press: the game samples its flags every frame, so
        // two frames reads as two presses.
        if held_bits != 0 {
            if let Some(addr) = flags_addr {
                let cur = m.mem.read32(addr);
                m.mem.poke32(addr, cur & !held_bits);
            }
            held_bits = 0;
        }

        // Scripted input, run at the same point in the frame as a keypress so it goes through the
        // identical path — a script that pressed buttons its own way would be testing itself.
        let mut script_shot = false;
        while script_at < script.len() && script[script_at].0 <= frames {
            let action = script[script_at].1.clone();
            script_at += 1;
            let bit = match action.as_str() {
                "select" => Some(BTN_SELECT),
                "menu" => Some(BTN_PREV),
                "play" => Some(BTN_PLAY),
                "next" => Some(BTN_NEXT),
                "prev" => Some(BTN_MENU),
                _ => None,
            };
            if let Some(bit) = bit {
                println!("script frame {frames}: {action} -> flags bit {bit:#04x}");
                if event_buttons {
                    post_event(&mut m, ctx_base, event_node, event_type_for(bit), 2, 0,
                               wheel_byte(wheel_raw));
                }
                if flags_addr.is_some() || !event_buttons {
                    press_button(&mut m, flags_addr, bit, wheel_byte(wheel_raw));
                    held_bits |= bit;
                }
                held_bits |= bit;
            } else if let Some(n) = action.strip_prefix("wheel") {
                let n: i32 = n.trim().parse().unwrap_or(0);
                // Queued one detent at a time, exactly as the trackpad path does: the game reads
                // a POSITION and needs it to change between polls, so a single jump reads as no
                // movement at all. This is the mistake that once killed scrolling outright.
                for _ in 0..n.abs() {
                    wheel_raw = (wheel_raw + n.signum()).rem_euclid(WHEEL_DETENTS);
                    m.queue_input(wheel_byte(wheel_raw));
                }
                println!("script frame {frames}: wheel {n:+} -> raw {wheel_raw}");
            } else if action == "shot" {
                script_shot = true;
            } else if action == "terminate" {
                // Exactly what the macOS Quit menu does: `exit()` without unwinding `main`. Here
                // so that path can be tested, since it is the one that leaves sound playing if
                // the reaper is not registered.
                println!("script frame {frames}: terminate");
                std::process::exit(0);
            } else if action == "quit" {
                println!("script frame {frames}: quit");
                script_quit = true;
            } else {
                println!("script frame {frames}: unknown action {action:?}, ignored");
            }
        }

        // The five click-wheel buttons, as flag bits. `0x18008304` onward tests 0x01, 0x02,
        // 0x04, 0x08 and 0x10 one at a time — five bits for five buttons — while 0x20 is the
        // wheel's own "event present". They were never entries in the event list.
        for &(key, name, bit) in buttons {
            if (live && window.is_key_pressed(key, minifb::KeyRepeat::No)) {
                println!("button {name:<11} -> flags bit {bit:#04x}");
                if event_buttons {
                    post_event(&mut m, ctx_base, event_node, event_type_for(bit), 2, 0,
                               wheel_byte(wheel_raw));
                }
                if flags_addr.is_some() || !event_buttons {
                    press_button(&mut m, flags_addr, bit, wheel_byte(wheel_raw));
                    held_bits |= bit;
                }
                held_bits |= bit;
            }
        }

        // Select: space bar or a left click, both edge-triggered so a held button is one press.
        let mouse_down = window.get_mouse_down(MouseButton::Left);
        let clicked = mouse_down && !mouse_was_down;
        mouse_was_down = mouse_down;
        if clicked || (live && window.is_key_pressed(Key::Space, minifb::KeyRepeat::No)) {
            // Two shots at Select, because the wheel byte demonstrably is not one:
            //   1. the event-word byte, kept for completeness (proven inert across all 256)
            //   2. the EVENT-TYPE byte at ctx+0x100 — the second context argument's first field,
            //      which the game branches on at 0x18018930 and which RetailOS's own frame pump
            //      compares against 5 and 6 at 0x0024e0dc. This is the better-founded guess.
            println!("button Select      -> flags bit {BTN_SELECT:#04x}");
            if event_buttons {
                post_event(&mut m, ctx_base, event_node, event_type_for(BTN_SELECT), 2, 0,
                           wheel_byte(wheel_raw));
            }
            if flags_addr.is_some() || !event_buttons {
                press_button(&mut m, flags_addr, BTN_SELECT, wheel_byte(wheel_raw));
            }
            held_bits |= BTN_SELECT;
        } else if event_hold > 0 {
            event_hold -= 1;
            if event_hold == 0 {
                // Retire the node — an empty list is a null head.
                m.mem.poke32(ctx_base + 0x30, 0);
            }
        }

        // Walk the Select candidate through the non-wheel bytes, to hunt it interactively.
        if (live && window.is_key_pressed(Key::RightBracket, minifb::KeyRepeat::No)) {
            select_idx = (select_idx + 1) % NON_WHEEL.len();
            select_byte = NON_WHEEL[select_idx];
            println!("Select candidate now {select_byte:#04x}");
        }
        if (live && window.is_key_pressed(Key::LeftBracket, minifb::KeyRepeat::No)) {
            select_idx = (select_idx + NON_WHEEL.len() - 1) % NON_WHEEL.len();
            select_byte = NON_WHEEL[select_idx];
            println!("Select candidate now {select_byte:#04x}");
        }

        // Two-finger trackpad swipe / mouse wheel, plus arrow keys as a keyboard equivalent.
        if let Some((_, sy)) = window.get_scroll_wheel().filter(|_| live) {
            scroll_accum += sy;
        }
        if (live && window.is_key_pressed(Key::Up, minifb::KeyRepeat::Yes)) {
            scroll_accum += scroll_per_detent;
        }
        if (live && window.is_key_pressed(Key::Down, minifb::KeyRepeat::Yes)) {
            scroll_accum -= scroll_per_detent;
        }
        // One queued event per detent crossed. Each poll consumes one, so a fast flick arrives as
        // a run of successive positions rather than a single jump — which is what a real wheel
        // does, and what the game's 16-entry contact ring is counting.
        // One queued event PER DETENT.
        //
        // Posting a single absolute position per frame is what the hardware does and it stopped
        // the wheel working entirely — the game evidently wants to see the position *change*
        // between polls, so a held-still position reads as no rotation. Queue each detent and let
        // the poll drain them, with a small ceiling so a hard flick cannot bank seconds of travel.
        const MAX_PENDING: usize = 4;
        let mut detents = 0i32;
        while scroll_accum.abs() >= scroll_per_detent && m.input_queue.len() < MAX_PENDING {
            let mut dir = if scroll_accum > 0.0 { -1 } else { 1 };
            if wheel_invert {
                dir = -dir;
            }
            scroll_accum -= scroll_accum.signum() * scroll_per_detent;
            wheel_raw = (wheel_raw + dir).rem_euclid(WHEEL_DETENTS);
            m.queue_input(wheel_byte(wheel_raw));
            detents += dir;
        }
        let ceiling = scroll_per_detent * 2.0;
        if scroll_accum.abs() > ceiling {
            scroll_accum = scroll_accum.signum() * ceiling;
        }
        if detents != 0 {
            println!(
                "wheel {} {:>2} detent(s) -> raw {wheel_raw:>3}, byte {:#04x}",
                if detents < 0 { "right/cw" } else { "left/ccw" },
                detents.abs(),
                wheel_byte(wheel_raw)
            );
        }

        // Shift+Q — the quit event, kept reachable but not adjacent to the play keys.
        if (live && window.is_key_pressed(Key::Q, minifb::KeyRepeat::No))
            && ((live && window.is_key_down(Key::LeftShift)) || (live && window.is_key_down(Key::RightShift)))
        {
            println!("event type 1 (QUIT)");
            post_event(&mut m, ctx_base, event_node, 1, 1, 0, wheel_byte(wheel_raw));
            event_hold = 2;
            last_node = event_node;
        }

        // P — write the panel to a PNG, so a rendering problem can be looked at without
        // photographing the whole desktop. Numbered, so a sequence can be captured.
        if script_shot || (live && window.is_key_pressed(Key::P, minifb::KeyRepeat::No)) {
            let path = format!("/tmp/ipod-shot-{shot_n:02}.png");
            let png = eapp_loader::png::encode(&m.framebuffer, FB_WIDTH, FB_HEIGHT);
            match fs::write(&path, png) {
                Ok(()) => println!("screenshot -> {path}  (frame {frames})"),
                Err(e) => println!("screenshot failed: {e}"),
            }
            // Print what the title has actually asked for by now: which framework entries it has
            // reached, and how often. A rendering fault is usually a call we are answering with 0,
            // so the census taken at the broken screen is the shortlist of what to implement.
            // Texture uploads: format/type/size for each, which is what a wrong decode shows up in.
            // Draw calls too: a flat-coloured background is usually a quad whose texture
            // coordinates arrived degenerate, which only the draw log shows.
            let draws: Vec<&String> = m.tex_log.iter().filter(|l| l.starts_with("n=")).collect();
            // --draws=N widens this: ten is enough to spot a degenerate background quad, but a
            // fault in small sprites needs a sample big enough to contain some.
            let want = args
                .iter()
                .find_map(|a| a.strip_prefix("--draws="))
                .and_then(|v| v.parse::<usize>().ok())
                .unwrap_or(10);
            // --dump-tex=N,M writes those textures out beside the screenshot.
            for spec in args.iter().filter_map(|a| a.strip_prefix("--dump-tex=")) {
                for t in spec.split(',') {
                    if let Ok(n) = t.trim().parse::<u32>() {
                        let p = format!("/tmp/ipod-tex-{n:02}.png");
                        match m.dump_texture(n, std::path::Path::new(&p)) {
                            Some((w, h)) => println!("  texture {n} -> {p} ({w}x{h})"),
                            None => println!("  texture {n}: not loaded"),
                        }
                    }
                }
            }
            println!("  texture list: {:?}", m.texture_list());
            println!(
                "  textures bound: {:?}",
                m.bound_ever.iter().collect::<Vec<_>>()
            );
            println!("  draws ({} logged, last {want}):", draws.len());
            for l in draws.iter().rev().take(want).rev() {
                println!("    {l}");
            }
            // Input events share this log and vastly outnumber the file operations, so filter
            // them out — otherwise a save attempt is invisible behind a wall of wheel positions.
            let fl: Vec<&String> =
                m.file_log.iter().filter(|l| !l.starts_with("input event")).collect();
            println!(
                "  sfx voices: {} of 4 sounding, {dropped_voices} trigger(s) dropped for a full pool",
                voices.len()
            );
            if m.watch.is_some() {
                let wl: Vec<_> = m.watch_log.iter().collect();
                println!("  memory writes ({} seen, last 12):", m.watch_log.census());
                for (pc, old, new) in wl.iter().rev().take(12).rev() {
                    println!("    by {pc:#010x}: {old:#010x} -> {new:#010x}");
                }
            }
            if !m.enter_pcs.is_empty() {
                let el: Vec<_> = m.enter_log.iter().collect();
                println!("  watched pcs ({} hits, last 10):", m.enter_log.census());
                for (pc, lr, a, n) in el.iter().rev().take(10).rev() {
                    println!(
                        "    {pc:#010x} <-{lr:#010x} r0-7:{:08x} {:08x} {:08x} {:08x} {:08x} {:08x} {:08x} {:08x} @{n}",
                        a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]
                    );
                }
            }
            // Where the frame is actually spending itself. The fault report has always printed
            // this, but a title that stalls without faulting never reaches it — and a stall is
            // exactly when you want to know which loop it is stuck in.
            {
                let recent = m.recent();
                let mut tally: std::collections::HashMap<u32, usize> = Default::default();
                for pc in &recent {
                    *tally.entry(*pc).or_default() += 1;
                }
                let mut top: Vec<_> = tally.into_iter().collect();
                top.sort_by_key(|&(_, n)| std::cmp::Reverse(n));
                let cells: Vec<String> =
                    top.iter().take(8).map(|(pc, n)| format!("{pc:#010x} x{n}")).collect();
                println!("  hottest recent pcs: {}", cells.join("  "));
            }
            println!("  file ops ({} of {} log entries, last 30):", fl.len(), m.file_log.census());
            for l in fl.iter().rev().take(30).rev() {
                println!("    {l}");
            }
            let tex: Vec<&String> = m.tex_log.iter().filter(|l| l.starts_with("texImage2D") || l.starts_with("upload tex") || l.starts_with("copyTexImage2D")).collect();
            println!("  texture uploads ({} logged, last 12):", tex.len());
            for l in tex.iter().rev().take(12).rev() {
                println!("    {l}");
            }
            // The last framework calls before the dump. When the game goes quiet, what it asked
            // for immediately beforehand is the thing that answered wrong.
            // The save-store calls specifically — they are rare, so a plain tail misses them.
            if !m.enter_log.is_empty() {
                println!("  arrivals at watched PCs: {}", m.enter_log.census());
                for &(pc, lr, a, n) in m.enter_log.iter().rev().take(12).rev() {
                    println!(
                        "    {pc:#010x} lr={lr:#010x} r0={:#010x} r1={:#010x} r2={:#010x} r3={:#010x} @{n}",
                        a[0], a[1], a[2], a[3]
                    );
                }
            } else if !m.enter_pcs.is_empty() {
                println!("  arrivals at watched PCs: 0  (armed, never reached)");
            }
            if let Some(nov) = &m.novelty {
                let mut v: Vec<(&u32, &u64)> = nov.iter().collect();
                v.sort_by_key(|(_, when)| **when);
                println!("  newest code reached (bucket, at instruction):");
                for (addr, when) in v.iter().rev().take(12).rev() {
                    println!("    {addr:#010x}  @{when}");
                }
                println!("  ({} buckets total, executed {})", nov.len(), m.executed);
            }
            // Audio calls, uncapped. Setup happens at load; the interesting ones — whatever means
            // "play this sound" — only fire during gameplay, so they have to be captured live.
            // The per-frame volume refresh (#2/#13/#14/#15 on handle 0) floods any window and
            // pushes the rare calls out. Filter it away so a trigger cannot hide behind it.
            let aud: Vec<&eapp_loader::Call> = m
                .trace
                .iter()
                .filter(|c| c.framework == "Audio")
                .filter(|c| !matches!(c.index, 2 | 13 | 14 | 15))
                .collect();
            println!("  Audio calls, excluding the per-frame refresh ({} total, last 20):", aud.len());
            for c in aud.iter().rev().take(20).rev() {
                println!(
                    "    #{:<4} r:{:08x} {:08x} {:08x} {:08x}  sp:{:08x} {:08x}  <-{:#010x}",
                    c.index, c.args[0], c.args[1], c.args[2], c.args[3],
                    c.stack[0], c.stack[1], c.return_to
                );
            }
            println!("  AsyncFileIO calls (last 10):");
            let afio: Vec<&eapp_loader::Call> =
                m.trace.iter().filter(|c| c.framework == "AsyncFileIO").collect();
            for c in afio.iter().rev().take(10).rev() {
                println!(
                    "    #{:<4} r:{:08x} {:08x} {:08x} {:08x}  sp:{:08x} {:08x} {:08x} {:08x}  <-{:#010x}",
                    c.index, c.args[0], c.args[1], c.args[2], c.args[3],
                    c.stack[0], c.stack[1], c.stack[2], c.stack[3], c.return_to
                );
            }
            println!("  last 24 framework calls:");
            for c in m.trace.iter().rev().take(24).rev() {
                println!(
                    "    {:<12} #{:<4} r:{:08x} {:08x} {:08x} {:08x}  sp:{:08x} {:08x}  <-{:#010x}",
                    c.framework, c.index,
                    c.args[0], c.args[1], c.args[2], c.args[3],
                    c.stack[0], c.stack[1], c.return_to
                );
            }
            if m.log_alloc && !m.enter_log.is_empty() {
        // Every arrival at a --watch-pc, totalled by call site and first argument. For a game
        // that suballocates inside its own heap, this is the only view of what it is asking for.
        let mut by: std::collections::BTreeMap<(u32, u32), (u64, u64)> = Default::default();
        for &(pc, lr, a, _) in m.enter_log.iter() {
            let e = by.entry((lr, a[0])).or_default();
            e.0 += 1;
            e.1 += a[0] as u64;
            let _ = pc;
        }
        let mut v: Vec<_> = by.into_iter().collect();
        v.sort_by_key(|(_, (_, bytes))| std::cmp::Reverse(*bytes));
        println!("  watched-pc arrivals by caller and r0 ({} total):", m.enter_log.census());
        for ((lr, arg), (n, bytes)) in v.iter().take(14) {
            println!("    from {lr:#010x}  r0={arg:<9} x{n:<6} = {bytes:>11} bytes");
        }
    }
    if m.log_alloc {
        let (mut a, mut f): (Vec<_>, Vec<_>) =
            (m.alloc_census.iter().collect(), m.free_census.iter().collect());
        a.sort_by_key(|(sz, n)| std::cmp::Reverse(**sz as u64 * **n));
        f.sort_by_key(|(sz, n)| std::cmp::Reverse(**sz as u64 * **n));
        println!("  allocations by total bytes (top 10):");
        for (sz, n) in a.iter().take(10) {
            println!("    {sz:>9} x{n:<7} = {:>11} bytes", **sz as u64 * **n);
        }
        println!("  releases by total bytes (top 10), {} rejected as not-ours:", m.free_rejected);
        for (sz, n) in f.iter().take(10) {
            println!("    {sz:>9} x{n:<7} = {:>11} bytes", **sz as u64 * **n);
        }
    }
    let reached = m.reached();
            let mut names: Vec<&&str> = reached.keys().collect();
            names.sort();
            println!("  imports reached at frame {frames}:");
            for n in names {
                let idxs = &reached[*n];
                println!("    {:<12} {:>3} of them: {:?}", n, idxs.len(), idxs);
            }
            shot_n += 1;
        }

        // Deliver any completion the host owes the game before the next frame. Two arguments:
        // the read completion asserts `arg0 == arg1 + 0x128` and spins on `b .` otherwise.
        let due: Vec<u32> = m.pending_completions.drain(..).collect();
        for req in due {
            let cb = m.mem.read32(req + eapp_loader::REQ_CALLBACK);
            let ctx_arg = m.mem.read32(req + eapp_loader::REQ_CONTEXT);
            m.file_log.push(format!(
                "completion req {req:#010x} cb {cb:#010x} ctx {ctx_arg:#010x}"
            ));
            if cb != 0 {
                let stop = m.call_with(cb, &[req, ctx_arg], budget);
                if !matches!(stop, Stop::Returned) {
                    m.file_log.push(format!("  completion did not return: {stop:?}"));
                }
            }
        }
        // Keep a wheel sample in flight every frame.
        //
        // The game only dispatches input on frames whose flags are non-zero, and only an
        // `InputEvents #0` poll that reports an event sets them. Sending one solely on a button
        // press means any screen we open — a pause menu, a dialog — has no way to receive the
        // next press. Real hardware reports continuously while a finger rests on the wheel, and
        // the position is unchanged so it reads as contact, not rotation.
        if m.input_queue.is_empty() {
            m.queue_input(wheel_byte(wheel_raw));
        }
        // Anything the game handed to a voice this frame: play the matching bank file.
        //
        // Crude on purpose — `afplay` per effect, no mixing, no latency control. It is the
        // shortest path from "the game asked for sound id N" to actually hearing it, and it
        // proves the id -> file mapping before any of a real mixer gets written.
        // Streams the game asked to play — the music. macOS decodes .m4a natively, so handing
        // the file to `afplay` is a real implementation of "play this stream", not a stand-in.
        let tracks: Vec<String> = m.audio_play_queue.drain(..).collect();
        for name in tracks {
            // Music arrives as a bare resource name to resolve against the game directory; a
            // sound effect arrives as the path the file was actually opened from, because the
            // buffer it was matched against came from that file.
            let path = match &m.game_dir {
                Some(dir) if !name.starts_with('/') => dir.join(&name),
                _ => PathBuf::from(&name),
            };
            let label = path
                .file_name()
                .and_then(|n| n.to_str())
                .unwrap_or(&name)
                .to_string();
            // Modes 1 (one) and 2 (all) both re-queue the finished item on the device; with a
            // single stream registered they are the same thing, and only mode 0 lets it stop.
            let repeat = m.music_repeat != 0;
            println!("audio: play {label}{}", if repeat { " (repeating)" } else { "" });
            if !path.exists() {
                println!("  (no such file: {})", path.display());
                continue;
            }
            // Starting a track stops whatever was playing. Without this the course music
            // layered over the title music, which are 110 and 45 seconds long and overlapped
            // audibly for the difference.
            if let Some(mut old) = music.take() {
                stop_child(&mut old.child);
            }
            if let Some(child) = play_file(&path) {
                music = Some(Music {
                    path,
                    label,
                    child,
                    repeat,
                });
            }
        }

        // Loop the background track. `afplay` exits at the end of the file, and the game never
        // asks again — it set the player's repeat flag once and expects the player to honour it,
        // which is why the music simply stopped after one track length.
        if let Some(cur) = &mut music {
            if matches!(cur.child.try_wait(), Ok(Some(_))) {
                reaper::forget(cur.child.id());
                match cur.repeat.then(|| play_file(&cur.path)).flatten() {
                    Some(child) => {
                        println!("audio: repeat {}", cur.label);
                        cur.child = child;
                    }
                    None => music = None,
                }
            }
        }
        // Sound effects, through a four-voice pool because that is what the device has.
        //
        // `0x00217a70` loops `cmp r4,#4`: when all four voices are busy, Apple's Play returns at
        // `0x001b91a8` without even setting the descriptor's state byte — a completely silent
        // drop. Modelling that is not a convenience, it is the behaviour: a hole of golf asks for
        // one effect fifty-odd times, and without the limit every one of them would be sounded.
        // A voice is busy here for as long as its `afplay` is still running.
        // Reap finished effects, restarting any whose repeat count was zero. A looping effect
        // holds its voice for as long as the game leaves it looping, which is what the device
        // does — Pac-Man's siren is one continuous sound for the whole level.
        let mut relaunch: Vec<(String, PathBuf)> = Vec::new();
        voices.retain_mut(|v: &mut (String, std::process::Child, Option<PathBuf>)| {
            if matches!(v.1.try_wait(), Ok(Some(_)) | Err(_)) {
                reaper::forget(v.1.id());
                if let Some(p) = &v.2 {
                    relaunch.push((v.0.clone(), p.clone()));
                }
                return false;
            }
            true
        });
        for (name, path) in relaunch {
            if let Some(child) = play_file(&path) {
                voices.push((name, child, Some(path)));
            }
        }
        // Stops first, so a stop and a re-trigger of the same effect in one frame ends as a
        // fresh voice rather than being swallowed by the "already sounding" check below.
        for name in m.sfx_stop_queue.drain(..) {
            voices.retain_mut(|v: &mut (String, std::process::Child, Option<PathBuf>)| {
                if v.0 != name {
                    return true;
                }
                let _ = v.1.kill();
                let _ = v.1.wait();
                reaper::forget(v.1.id());
                println!("sfx: stop {name}");
                false
            });
        }
        for (name, looping) in m.sfx_queue.drain(..) {
            let path = match &m.game_dir {
                Some(dir) => dir.join(&name),
                None => PathBuf::from(&name),
            };
            // One voice per sound, as on the device: a descriptor holds a single attached voice
            // (`+0x34`), so re-triggering one that is still sounding restarts it rather than
            // layering a second copy over itself.
            if voices.iter().any(|(n, _, _)| *n == name) {
                continue;
            }
            if voices.len() >= 4 {
                dropped_voices += 1;
                continue;
            }
            if !path.exists() {
                println!("sfx: no such file: {}", path.display());
                continue;
            }
            if let Some(child) = play_file(&path) {
                println!("sfx: play {name}{}", if looping { " (looping)" } else { "" });
                voices.push((name, child, looping.then_some(path)));
            }
        }

        // RetailOS's frame pump refills the reason byte before EVERY call (`[ctx+0x00] = 5 or 4`,
        // from `0x0024da80`), not once at startup. Left alone it drifts: Lost writes it back from
        // its own state at `0x1803d844` and thereafter sees 0 or 1, which routes it down an
        // event-drain path that renders only its splash overlay.
        // `--frame-reason=auto`: the reason byte is HALF of a two-way handshake, and treating
        // it as a constant is what leaves The Sims Bowling rebuilding itself forever.
        //
        // RetailOS's pump (`0x0024dadc`) writes 5 when its manager is in state 1, 4 when state 3,
        // and **leaves the byte alone otherwise** — so the first call sees the zero the manager
        // was allocated with. The game's own dispatcher at `0x18045740` reads it:
        //
        //   0 -> drain the event list, then run the FULL application init (`0x180052d4`)
        //   1 -> the normal per-frame path (`0x18045794`)
        //   5 -> the suspend/resume path, which answers 6
        //
        // and answers in the byte at `ctx+0x100` (`0x1804578c` writes 1 after init). So the OS is
        // meant to see that answer and stop asking for init. With a constant 0 the game inits on
        // every frame, never destroys what it built, and exhausts its fixed 5.24 MB heap in 75
        // iterations; with a constant 1 it never inits at all and sits idle. Ask for init until
        // the game says it is done, then ask for frames.
        // `auto` — the reason byte is HALF of a two-way handshake, and a constant is what
        // leaves The Sims Bowling rebuilding itself forever.
        //
        // RetailOS's pump (`0x0024dadc`) writes 5 when its manager is in state 1, 4 when state 3,
        // and **leaves the byte alone otherwise** — so the first call sees the zero the manager
        // was allocated with, and after that the byte carries whatever the game last put there.
        // The game's dispatcher at `0x18045740` reads it:
        //
        //   0 -> drain the event list, then run the FULL application init (`0x180052d4`)
        //   1 -> the normal per-frame path (`0x18045794`)
        //   5 -> the suspend/resume path, which answers 6
        //
        // and answers in the byte at `ctx+0x100` (`0x1804578c` writes 1 after init). With a
        // constant 0 the game inits on every frame, never destroys what it built, and exhausts
        // its fixed 5.24 MB heap in 75 iterations; with a constant 1 it never inits at all. Ask
        // for init until the game says it is done, then ask for frames.
        if reason_auto {
            // "Answered" means the byte is no longer what we left there. With no seed that is
            // simply non-zero; with `--pump-mark` it is "different from the seed", because the
            // seed is itself non-zero and would otherwise read as an answer on frame one.
            let answered = m.mem.read8(ctx_base + answer_off) != pump_mark.unwrap_or(0);
            reason_last = if answered { reason_steady } else { 0 };
            m.mem.poke8(ctx_base + reason_off, reason_last);
            let _ = reason_ours;
        } else if let Some(r) = per_frame_reason {
            m.mem.poke8(ctx_base + reason_off, r);
        }
        let stop = m.call_with(frame_vector, &ctx, budget);
        flush_call_log(&mut call_log, &m.trace, frames);
        flush_enter_log(&mut enter_log, &mut m);
        frames += 1;

        // The emulator's framebuffer is packed RGB; the window wants 0RGB words.
        for (i, px) in m.framebuffer.chunks_exact(3).enumerate() {
            buf[i] = ((px[0] as u32) << 16) | ((px[1] as u32) << 8) | px[2] as u32;
        }
        let _ = window.update_with_buffer(&buf, FB_WIDTH, FB_HEIGHT);

        // The window title carries the live rate; the log line stays, but at a calmer cadence.
        if last_report.elapsed() > Duration::from_millis(500) {
            let fps = frames as f64 / started.elapsed().as_secs_f64();
            window.set_title(&format!("{title} — iPod 5G — frame {frames} — {fps:.1} fps"));
            if last_log.elapsed() > Duration::from_secs(2) {
                println!(
                    "frame {frames}  {fps:.1} fps  {} quads  {} instructions",
                    m.quads_drawn, m.executed
                );
                last_log = Instant::now();
            }
            last_report = Instant::now();
        }
        if !matches!(stop, Stop::Returned) {
            println!("stopped: {stop:?} after {frames} frames");
            println!(
                "  pending completions: {}  open handles: {}",
                m.pending_completions.len(),
                m.open_file_count()
            );
            println!("  heap used: {} bytes", m.heap_used());
            // Unmapped WRITES are the ones that silently lose data: a game storing a flag into a
            // region we never modelled reads it back as zero forever. That is exactly how the
            // Lost(0) cluster arose (Sudoku kept a state byte in PP5022 IRAM at 0x4000003d).
            let mut pages: Vec<_> =
                m.mem.unmapped.iter().filter(|(_, p)| p.writes > 0).collect();
            pages.sort_by_key(|(_, p)| std::cmp::Reverse(p.writes));
            if !pages.is_empty() {
                println!("  unmapped WRITES (top 6 pages):");
                for (base, p) in pages.iter().take(6) {
                    let pc = p.pcs.iter().max_by_key(|(_, n)| **n).map(|(k, _)| *k).unwrap_or(0);
                    println!(
                        "    {:#010x}..{:#010x}  {} writes, hottest pc {pc:#010x}",
                        p.lo, p.hi, p.writes
                    );
                    let _ = base;
                }
            }
            // The register file at the fault. For a `Lost` stop this says which pointer was
            // followed into nothing, which the PC history alone cannot.
            let r = &m.cpu.regs;
            for row in 0..4 {
                let cells: Vec<String> = (0..4)
                    .map(|c| format!("r{:<2}={:08x}", row * 4 + c, r[row * 4 + c]))
                    .collect();
                println!("    {}", cells.join("  "));
            }
            // Where it was spinning: the distinct addresses in the recent history, most common
            // first. A hang shows up as a handful of addresses repeated thousands of times.
            let recent = m.recent();
            let mut tally: std::collections::HashMap<u32, usize> = std::collections::HashMap::new();
            for pc in &recent {
                *tally.entry(*pc).or_default() += 1;
            }
            let mut top: Vec<_> = tally.into_iter().collect();
            top.sort_by_key(|&(_, n)| std::cmp::Reverse(n));
            let span = recent.iter().copied().min().unwrap_or(0)
                ..=recent.iter().copied().max().unwrap_or(0);
            println!(
                "  spinning in {:#010x}..={:#010x}; hottest:",
                span.start(),
                span.end()
            );
            for (pc, n) in top.iter().take(8) {
                println!("    {pc:#010x} x{n}");
            }
            // The LAST addresses executed, in order. For a `Lost` stop this is the branch that
            // went nowhere and the handful of instructions that set it up.
            let tail: Vec<String> =
                recent.iter().rev().take(10).rev().map(|p| format!("{p:#010x}")).collect();
            println!("  last executed: {}", tail.join(" -> "));
            if m.watch.is_some() {
                let wl: Vec<_> = m.watch_log.iter().collect();
                println!("  memory writes ({} seen, last 12):", m.watch_log.census());
                for (pc, old, new) in wl.iter().rev().take(12).rev() {
                    println!("    by {pc:#010x}: {old:#010x} -> {new:#010x}");
                }
            }
            if !m.enter_pcs.is_empty() {
                println!("  watched pcs ({} hits, last 6):", m.enter_log.census());
                let el: Vec<_> = m.enter_log.iter().collect();
                for (pc, lr, a, n) in el.iter().rev().take(6).rev() {
                    println!(
                        "    {pc:#010x} <-{lr:#010x} r0-7:{:08x} {:08x} {:08x} {:08x} {:08x} {:08x} {:08x} {:08x} @{n}",
                        a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]
                    );
                }
            }
            silence(&mut voices, &mut music, |m| stop_child(&mut m.child));
            // Keep the window up so the last frame stays visible — but not when a script is
            // driving, because a scripted run is unattended by definition and this loop is what
            // turned "the title faulted" into "the harness hung until its timeout", with the
            // buffered log thrown away when it was killed.
            if script.is_empty() {
                while window.is_open() && !(live && window.is_key_down(Key::Escape)) {
                    let _ = window.update_with_buffer(&buf, FB_WIDTH, FB_HEIGHT);
                    std::thread::sleep(target);
                }
            }
            break;
        }
    }

    // One line, always, whichever way the run ended. Comparing two builds over eighteen titles
    // needs a summary that is present even when the title died, not a periodic progress report
    // that stops when it does.
    println!(
        "summary: {title} frames={frames} quads={} clears={} instructions={}",
        m.quads_drawn, m.clears, m.executed
    );
    // Which imports the run actually reached. A title that draws nothing has either not asked
    // for anything yet or asked and been answered badly, and those are different bugs — this is
    // what tells them apart without a second run under the tracer.
    if m.log_alloc && !m.enter_log.is_empty() {
        // Every arrival at a --watch-pc, totalled by call site and first argument. For a game
        // that suballocates inside its own heap, this is the only view of what it is asking for.
        let mut by: std::collections::BTreeMap<(u32, u32), (u64, u64)> = Default::default();
        for &(pc, lr, a, _) in m.enter_log.iter() {
            let e = by.entry((lr, a[0])).or_default();
            e.0 += 1;
            e.1 += a[0] as u64;
            let _ = pc;
        }
        let mut v: Vec<_> = by.into_iter().collect();
        v.sort_by_key(|(_, (_, bytes))| std::cmp::Reverse(*bytes));
        println!("  watched-pc arrivals by caller and r0 ({} total):", m.enter_log.census());
        for ((lr, arg), (n, bytes)) in v.iter().take(14) {
            println!("    from {lr:#010x}  r0={arg:<9} x{n:<6} = {bytes:>11} bytes");
        }
    }
    if m.log_alloc {
        let (mut a, mut f): (Vec<_>, Vec<_>) =
            (m.alloc_census.iter().collect(), m.free_census.iter().collect());
        a.sort_by_key(|(sz, n)| std::cmp::Reverse(**sz as u64 * **n));
        f.sort_by_key(|(sz, n)| std::cmp::Reverse(**sz as u64 * **n));
        println!("  allocations by total bytes (top 10):");
        for (sz, n) in a.iter().take(10) {
            println!("    {sz:>9} x{n:<7} = {:>11} bytes", **sz as u64 * **n);
        }
        println!("  releases by total bytes (top 10), {} rejected as not-ours:", m.free_rejected);
        for (sz, n) in f.iter().take(10) {
            println!("    {sz:>9} x{n:<7} = {:>11} bytes", **sz as u64 * **n);
        }
    }
    let reached = m.reached();
    let mut names: Vec<&&str> = reached.keys().collect();
    names.sort();
    for n in names {
        println!("  reached {:<12} {:?}", n, reached[*n]);
    }
    if let (Some(path), Some(edges)) = (&callgraph_dump, &m.edges) {
        let mut out = String::new();
        for ((site, tgt), n) in edges {
            out.push_str(&format!("{site:08x} {tgt:08x} {n}\n"));
        }
        match std::fs::write(path, out) {
            Ok(()) => println!("  callgraph: {} distinct edges -> {path}", edges.len()),
            Err(e) => println!("  callgraph: {path}: {e}"),
        }
    }

    // Nothing the emulator started should outlive it.
    silence(&mut voices, &mut music, |m| stop_child(&mut m.child));
}

/// Stop every `afplay` this process started.
///
/// `kill` then `wait`: without the wait each one stays a zombie until the emulator itself exits,
/// which is harmless here but leaves `ps` showing sound that is no longer playing. Errors are
/// ignored on purpose — a process that already finished on its own is exactly the common case.
fn silence<M>(
    voices: &mut Vec<(String, std::process::Child, Option<PathBuf>)>,
    music: &mut Option<M>,
    kill: impl Fn(&mut M),
) {
    for v in voices.iter_mut() {
        stop_child(&mut v.1);
    }
    voices.clear();
    if let Some(m) = music.as_mut() {
        kill(m);
    }
    *music = None;
    // Belt and braces: anything spawned but not in either collection dies here too.
    reaper::reap_all();
}
