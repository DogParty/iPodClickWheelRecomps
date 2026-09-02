// The desktop platform on SDL3: a scaled window for the 320×240 screen, the keyboard as the click
// wheel, and SDL audio streams for both the sound effects and the music.
//
// The keyboard is the only way in. A mouse wheel and a trackpad were once read as the click wheel
// as well, and are not any more: what a trackpad sends cannot be told apart from what a swipe up
// the pad sends by accident (a quarter of the travel of an upward swipe arrives on the sideways
// axis, and its tail arrives on that axis alone), so a gesture nobody bound kept turning the menu.
// Every control is now one a player can see and change in Settings ▸ Input.
//
// The defaults, all rebindable:
//   ← / →                  turn the click wheel (one row a press)
//   Space                  Select (centre button)
//   ↑                      Menu    [  Previous    ↓  Play/Pause    ]  Next
//   P                      screenshot  Q  quit
//   - / =                  step the window through whole multiples; F or F11 full screen
//
#include "platform/input_bindings.h"
#include "platform/platform.h"
#include "platform/sdl3/macos_settings.h"
#include "platform/sdl3/music_decoder.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace holdem::platform {

namespace {

constexpr int WINDOW_SCALE = 3;
// The wheel has 120 detents to a turn and a menu moves one row every eight of them (the oracle
// scripts turn it in eights for exactly this reason), so a key press is worth a row. One detent a
// press meant eight presses per letter.
constexpr int DETENTS_PER_ROW = 8;

// The keys offered in the settings window. F, F11, L, P and Q are left out: they are the window's
// own and the program's, and a player who bound one would lose the shortcut. Escape is offered —
// it is the Menu button by default, which is what a player reaches for to back out of a screen.
constexpr unsigned ASSIGNABLE_KEY_COUNT = 48;
// A ceiling on Sharp's intermediate texture: eight times 320x240 is 2560x1920, enough to cover
// any screen worth playing this on, and a bound on what a wildly resized window can ask for.
constexpr int MAX_PRESCALE = 8;
// Halvings on the way down from a picture larger than the window. Render scale 8 into a window at
// the game's own size is three; the cap is only here so that a bad output size cannot loop.
constexpr unsigned MAX_REDUCTION_STEPS = 4;

// HOLDEM_TRACE_INPUT=1 prints every key the window receives, with what it is bound to. What a
// device actually sends differs by device and by the system's own settings, and there is no other
// way to see it from here; three rounds of guessing at a rebinding fault taught that lesson.
bool trace_input() {
    static const bool on = SDL_getenv("HOLDEM_TRACE_INPUT") != nullptr;
    return on;
}

// HOLDEM_TRACE_AUDIO=1 prints what the audio is asked to do — the track opened and its shape, the
// loops, the stops, the volume. Sound that does not come out has no other symptom, and there is
// nothing on screen to read: this is the difference between seeing the answer and guessing at it,
// as the input trace above it is. It came with the music player from the Mini Golf recomp.
bool trace_audio() {
    static const bool on = SDL_getenv("HOLDEM_TRACE_AUDIO") != nullptr;
    return on;
}
constexpr size_t VOICE_LIMIT = 4;                 // the device's sound-effect polyphony
constexpr Uint64 TITLE_REFRESH_NS = 500'000'000;  // how often the frame rate in the title updates

// A sound effect playing on its own SDL audio stream.
// A sound effect's samples, read from its .wav once. The game plays the same handful of sounds
// over and over — every menu row moved is one — so the file is read once and kept.
// The game's own format, as SDL spells it. It uses the two the hardware took — eight-bit
// unsigned and sixteen-bit signed — and says which in the sound's description.
bool spec_for(const SoundClip& clip, SDL_AudioSpec& spec) {
    if (clip.bits != 8 && clip.bits != 16) {
        return false;
    }
    spec.format = clip.bits == 8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE;
    spec.channels = static_cast<int>(clip.channels);
    spec.freq = static_cast<int>(clip.sample_rate);
    return spec.channels > 0 && spec.freq > 0;
}

// One sound playing, on an audio stream of its own.
//
// The stream is opened once and kept for the life of the program, which is the whole point of
// this class: opening one costs about 15 ms — most of a frame at 60 Hz — while putting samples
// into one that is already open costs nothing measurable. A voice that has finished goes quiet
// and waits to be used again rather than being destroyed.
class Voice {
public:
    ~Voice() { close(); }

    // Give the stream back to SDL, and be able to say when.
    //
    // A destructor is too late. Members are destroyed *after* the body of the destructor that
    // owns them, and that body is where `SDL_Quit` is — so a voice left to clean itself up
    // destroys its stream after the audio subsystem it belongs to has gone, and dereferences
    // freed memory on the way out of the program. It is a crash that only happens once a sound
    // has actually played, which is why it went unseen: a voice that never opened a stream has
    // nothing to destroy.
    void close() {
        if (stream_ != nullptr) {
            SDL_DestroyAudioStream(stream_);
            stream_ = nullptr;
        }
        samples_.clear();
    }

    Voice() = default;
    Voice(const Voice&) = delete;
    Voice& operator=(const Voice&) = delete;

    [[nodiscard]] bool idle() const { return samples_.empty(); }
    [[nodiscard]] uint32_t sounding() const { return voice_; }

    // Start (or restart) this voice on `clip`. False if there is no audio device to play it on.
    //
    // The samples are copied rather than referred to: the buffer belongs to the game, which is
    // free to load something else over it the moment this returns, and a looping voice has to be
    // able to put them in again later.
    bool start(uint32_t voice, const SoundClip& clip, bool looping) {
        SDL_AudioSpec spec{};
        if (!spec_for(clip, spec)) {
            std::fprintf(stderr, "sound: voice %u has no format to play (%u-bit, %u channel)\n",
                         voice, clip.bits, clip.channels);
            return false;
        }
        if (stream_ == nullptr) {
            stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr,
                                                nullptr);
            if (stream_ == nullptr) {
                std::fprintf(stderr, "sound: no audio device: %s\n", SDL_GetError());
                return false;
            }
        } else if (!SDL_SetAudioStreamFormat(stream_, &spec, nullptr)) {
            // A sound in a format the open stream cannot take: rare, and not worth a new device.
            std::fprintf(stderr, "sound: cannot play voice %u: %s\n", voice, SDL_GetError());
            return false;
        }
        SDL_ClearAudioStream(stream_);
        voice_ = voice;
        samples_.assign(clip.samples, clip.samples + clip.bytes);
        looping_ = looping;
        SDL_PutAudioStreamData(stream_, samples_.data(), static_cast<int>(samples_.size()));
        SDL_ResumeAudioStreamDevice(stream_);
        return true;
    }

    void stop() {
        if (stream_ != nullptr) {
            SDL_ClearAudioStream(stream_);
        }
        samples_.clear();
    }

    // A looping voice refills its queue; a one-shot voice falls idle when drained.
    void service() {
        if (samples_.empty() || stream_ == nullptr) {
            return;
        }
        if (SDL_GetAudioStreamQueued(stream_) > 0) {
            return;
        }
        if (looping_) {
            SDL_PutAudioStreamData(stream_, samples_.data(), static_cast<int>(samples_.size()));
            return;
        }
        samples_.clear();
    }

private:
    uint32_t voice_ = 0;
    std::vector<uint8_t> samples_;
    bool looping_ = false;
    SDL_AudioStream* stream_ = nullptr;
};

// The music, decoded here and fed to SDL like the sound effects are.
//
// This used to spawn `afplay`, which played the file and offered nothing else: no volume
// this program could set, no way to stop it that was not a signal, and macOS only. The
// Mini Golf recomp had already replaced that with a real decoder, and this is that same
// class over the same shared `MusicDecoder` — see ../../../../common/README.md. The old
// TODO here asked for exactly this and is answered.
// The game's music, on the same audio device as everything else.
//
// The tracks are AAC (`.m4a`), which SDL does not decode; `music_decoder.h` does that and hands
// back PCM, and this feeds it to an SDL audio stream a chunk at a time exactly as a sound effect
// is fed. It used to be a child `afplay` process, which played the file and offered nothing
// else: no volume this program could set, no way to stop it that was not a signal, and macOS
// only. Everything the game asks of the music — stop it, turn it down — needs the audio to be
// ours, so it is.
//
// A track is decoded as it plays. `service()` tops the stream up to about half a second ahead,
// which is far more than a frame's worth of slack and small enough that no track costs more
// than a few hundred kilobytes of buffer.
class MusicPlayer {
public:
    ~MusicPlayer() { close_stream(); }

    MusicPlayer() = default;
    MusicPlayer(const MusicPlayer&) = delete;
    MusicPlayer& operator=(const MusicPlayer&) = delete;

    void play(const std::string& path, bool repeat) {
        stop();
        if (!music_decoding_supported()) {
            if (!warned_) {
                warned_ = true;
                std::fprintf(stderr, "music: no decoder in this build (wanted %s)\n", path.c_str());
            }
            return;
        }
        SDL_AudioSpec spec{};
        if (!decoder_.open(path, spec)) {
            return;  // the decoder has said why
        }
        if (trace_audio()) {
            std::fprintf(stderr, "audio: music %s (%d Hz, %d ch)%s\n", path.c_str(), spec.freq,
                         spec.channels, repeat ? ", repeating" : "");
        }
        repeat_ = repeat;
        if (!open_stream(spec)) {
            decoder_.close();
            return;
        }
        service();  // start with the queue filled, so the track opens without a gap
        SDL_ResumeAudioStreamDevice(stream_);
    }

    // Silence it until another track is asked for. What Music: OFF does.
    void stop() {
        if (trace_audio() && decoder_.is_open()) {
            std::fprintf(stderr, "audio: music stopped\n");
        }
        if (stream_ != nullptr) {
            SDL_ClearAudioStream(stream_);
            SDL_PauseAudioStreamDevice(stream_);
        }
        decoder_.close();
    }

    // Called each frame: keep the queue ahead of the device, and loop or finish at the end.
    void service() {
        if (!decoder_.is_open() || stream_ == nullptr) {
            return;
        }
        while (SDL_GetAudioStreamQueued(stream_) < static_cast<int>(QUEUE_TARGET_BYTES)) {
            const int frames = decoder_.read(buffer_.data(), CHUNK_FRAMES);
            if (frames <= 0) {
                if (!repeat_) {
                    decoder_.close();
                    return;
                }
                if (trace_audio()) {
                    std::fprintf(stderr, "audio: music looped\n");
                }
                decoder_.restart();
                // A track that decodes to nothing at all would spin here for ever.
                if (decoder_.read(buffer_.data(), CHUNK_FRAMES) <= 0) {
                    decoder_.close();
                    return;
                }
                continue;
            }
            SDL_PutAudioStreamData(stream_, buffer_.data(), frames * bytes_per_frame_);
        }
    }

    // The device's volume, 0 to 1. Kept for the next track as well as applied to this one.
    void set_gain(float gain) {
        gain_ = gain;
        if (stream_ != nullptr) {
            (void)SDL_SetAudioStreamGain(stream_, gain_);
        }
    }

    // How much is queued ahead of the device, for the trace and for the tests.
    [[nodiscard]] int queued_bytes() const {
        return stream_ == nullptr ? 0 : SDL_GetAudioStreamQueued(stream_);
    }

private:
    static constexpr int CHUNK_FRAMES = 8192;
    static constexpr size_t QUEUE_TARGET_BYTES = 88'200;  // about half a second of 44.1 kHz stereo

    bool open_stream(const SDL_AudioSpec& spec) {
        // A new track may be a different shape from the last; reopening only when it is keeps
        // the usual case (six tracks, all 44.1 kHz stereo) down to one device open per run.
        if (stream_ != nullptr && (spec.format != spec_.format || spec.channels != spec_.channels ||
                                   spec.freq != spec_.freq)) {
            close_stream();
        }
        if (stream_ == nullptr) {
            stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr,
                                                nullptr);
            if (stream_ == nullptr) {
                std::fprintf(stderr, "music: no audio device: %s\n", SDL_GetError());
                return false;
            }
            (void)SDL_SetAudioStreamGain(stream_, gain_);
        }
        spec_ = spec;
        bytes_per_frame_ =
            static_cast<int>(SDL_AUDIO_BYTESIZE(spec.format)) * static_cast<int>(spec.channels);
        buffer_.assign(static_cast<size_t>(CHUNK_FRAMES) * static_cast<size_t>(bytes_per_frame_),
                       0);
        return true;
    }

    void close_stream() {
        decoder_.close();
        if (stream_ != nullptr) {
            SDL_DestroyAudioStream(stream_);
            stream_ = nullptr;
        }
    }

    MusicDecoder decoder_;
    SDL_AudioStream* stream_ = nullptr;
    SDL_AudioSpec spec_{};
    std::vector<Uint8> buffer_;
    int bytes_per_frame_ = 4;
    bool repeat_ = false;
    bool warned_ = false;
    float gain_ = 1.0f;
};

// SDL is shut down when this goes.
//
// It exists so that the shutdown can be made to happen *last* by declaring it first: members are
// destroyed in reverse order of declaration, and the destructor body runs before any of them. Put
// `SDL_Quit` in that body — which is where it was — and every member holding something of SDL's
// is destroyed after the subsystem it belongs to has gone. A voice with an audio stream did
// exactly that and took the program down on its way out, and only once a sound had actually
// played, since a voice that never opened a stream has nothing to give back.
struct SdlSession {
    ~SdlSession() { SDL_Quit(); }
};

class Sdl3Platform final : public Platform {
public:
    Sdl3Platform(const char* title, unsigned frames_per_second)
        : title_(title), paced_rate_(frames_per_second == 0 ? 60 : frames_per_second) {
        // The rate this run was started at is the default until a saved one is read over it
        // (runtime/main.cpp).
        settings().frame_rate = frames_per_second;
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
            std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return;
        }
        if (!SDL_CreateWindowAndRenderer(title, static_cast<int>(SCREEN_WIDTH) * WINDOW_SCALE,
                                         static_cast<int>(SCREEN_HEIGHT) * WINDOW_SCALE,
                                         SDL_WINDOW_RESIZABLE, &window_, &renderer_)) {
            std::fprintf(stderr, "cannot create window: %s\n", SDL_GetError());
            return;
        }
        // The keys this platform starts with. A player's own bindings, if there are any, are
        // loaded over these once the save store is in place (runtime/main.cpp).
        //
        // The four arrows go to the four *sides* of the wheel, because that is what this game
        // asks for by name — it walks by touch, not by rotation — and an arrow key is what a
        // player reaches for to walk. Turning the wheel, which is what moves a menu, therefore
        // cannot have them: an input does exactly one thing. It gets `,` and `.`, which sit
        // beside each other and read as previous and next; anyone who would rather have it the
        // other way round can say so in Settings > Input.
        const InputCode defaults[ACTION_COUNT][BINDING_SLOTS] = {
            {SDLK_COMMA, NO_INPUT},         // SwipeLeft
            {SDLK_PERIOD, NO_INPUT},        // SwipeRight
            {SDLK_UP, NO_INPUT},            // TouchUp
            {SDLK_RIGHT, NO_INPUT},         // TouchRight
            {SDLK_DOWN, NO_INPUT},          // TouchBottom
            {SDLK_LEFT, NO_INPUT},          // TouchLeft
            {SDLK_SPACE, NO_INPUT},         // Select: Return finishes a typed name
            {SDLK_TAB, NO_INPUT},           // PlayPause
            {SDLK_ESCAPE, NO_INPUT},        // Menu
            {SDLK_LEFTBRACKET, NO_INPUT},   // Rewind
            {SDLK_RIGHTBRACKET, NO_INPUT},  // FastForward
        };
        set_default_bindings(defaults);
        set_assignable_inputs(assignable_keys(), ASSIGNABLE_KEY_COUNT);
        // Typing is how a name gets entered on a machine with a keyboard; spelling a name out
        // on the wheel keys still works.
        SDL_StartTextInput(window_);
        // The window keeps the screen's shape however it is dragged, so the picture fills it and
        // there is nothing to letterbox. - and = step it through whole multiples of 320x240.
        const float shape = static_cast<float>(SCREEN_WIDTH) / static_cast<float>(SCREEN_HEIGHT);
        SDL_SetWindowAspectRatio(window_, shape, shape);
        // Locking the shape can leave the window a size of the system's choosing, so ask for the
        // one that was wanted again afterwards.
        SDL_SetWindowSize(window_, static_cast<int>(SCREEN_WIDTH) * WINDOW_SCALE,
                          static_cast<int>(SCREEN_HEIGHT) * WINDOW_SCALE);

        // Rebinding writes straight into the table the key handling reads, so saving is all the
        // window asks of us; the frame rate it does not own at all, and only asks us to flip.
        SettingsHooks hooks;
        hooks.on_bindings_changed = [](void*) { save_input_bindings(); };
        hooks.on_frame_rate_chosen = [](void* context, unsigned rate) {
            static_cast<Sdl3Platform*>(context)->set_frame_rate(rate);
        };
        hooks.on_show_frame_rate_changed = [](void* context, bool show) {
            static_cast<Sdl3Platform*>(context)->set_show_frame_rate(show);
        };
        hooks.frame_rate = settings().frame_rate;
        hooks.show_frame_rate = settings().show_frame_rate;
        hooks.on_scaling_chosen = [](void* context, Scaling scaling) {
            static_cast<Sdl3Platform*>(context)->set_scaling(scaling);
        };
        hooks.on_pixel_perfect_changed = [](void* context, bool pixel_perfect) {
            static_cast<Sdl3Platform*>(context)->set_pixel_perfect(pixel_perfect);
        };
        hooks.on_render_scale_chosen = [](void* context, unsigned scale) {
            static_cast<Sdl3Platform*>(context)->set_render_scale(scale);
        };
        hooks.on_high_resolution_text_changed = [](void* context, bool resolve) {
            static_cast<Sdl3Platform*>(context)->set_high_resolution_text(resolve);
        };
        hooks.on_unlock_all_chapters_changed = [](void* context, bool unlock) {
            static_cast<Sdl3Platform*>(context)->set_unlock_all_chapters(unlock);
        };
        hooks.scaling = settings().scaling;
        hooks.pixel_perfect = settings().pixel_perfect;
        hooks.render_scale = settings().render_scale;
        hooks.high_resolution_text = settings().high_resolution_text;
        hooks.unlock_all_chapters = settings().unlock_all_chapters;
        hooks.context = this;
        macos_settings_install(hooks);
        ensure_texture(SCREEN_WIDTH, SCREEN_HEIGHT);
        apply_presentation();
        // Show black until the game has drawn something.
        //
        // The frame pump does not present a frame the game drew nothing into (gfx::anything_drawn)
        // — its first frame is the firmware telling it that it is running, and the buffer is still
        // the magenta that marks an un-drawn region. Without this the window would instead show
        // whatever the renderer happened to start with for that one frame, which is not something
        // to leave to chance in the first thing a player sees.
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);
        SDL_RenderPresent(renderer_);
        next_frame_ns_ = SDL_GetTicksNS();
        title_updated_ns_ = next_frame_ns_;
    }

    // What this body releases is what nothing else would: the window, the renderer and their
    // textures are raw handles with no owner. Everything with a destructor of its own — a voice
    // and its audio stream — is released after this runs and before SDL goes, which is what
    // `sdl_session_` is for.
    ~Sdl3Platform() override {
        if (prescale_ != nullptr) {
            SDL_DestroyTexture(prescale_);
        }
        for (SDL_Texture* step : reduce_) {
            if (step != nullptr) {
                SDL_DestroyTexture(step);
            }
        }
        if (texture_ != nullptr) {
            SDL_DestroyTexture(texture_);
        }
        if (renderer_ != nullptr) {
            SDL_DestroyRenderer(renderer_);
        }
        if (window_ != nullptr) {
            SDL_DestroyWindow(window_);
        }
    }

    void poll(FrameInput& input) override {
        input = FrameInput{};
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                input.quit = true;
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                // The shape is SDL's to keep (SDL_SetWindowAspectRatio, above); the whole
                // multiple, if the player asked for one, is ours.
                snap_window_to_whole_multiple();
                break;
            case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
                // Dragged to another screen, which may refresh at another rate: what a whole
                // number of refreshes means has changed with it.
                apply_frame_pacing();
                break;
            case SDL_EVENT_KEY_DOWN:
                // Command-comma opens the settings window. The menu item carries the same
                // shortcut, but whether a key equivalent reaches the menu depends on how the
                // window that has focus was made; handling it here works either way.
                if (event.key.key == SDLK_COMMA && (event.key.mod & SDL_KMOD_GUI) != 0 &&
                    !event.key.repeat) {
                    macos_settings_open();
                    break;
                }
                // Backspace and Return belong to whatever is being typed. Neither is offered in
                // the settings window, so neither can be a control.
                if (event.key.key == SDLK_BACKSPACE) {
                    ++input.typed.backspaces;
                    break;
                }
                if (event.key.key == SDLK_RETURN && !event.key.repeat) {
                    input.typed.confirm = true;
                    break;
                }
                // What the player has bound comes first, so any key can be rebound to anything;
                // the window's own shortcuts only get the keys nothing else wants.
                if (handle_key(event.key.key, event.key.repeat, input)) {
                    swallow_next_text_ = true;
                    break;
                }
                if (event.key.repeat) {
                    break;
                }
                if (event.key.key == SDLK_F || event.key.key == SDLK_F11) {
                    toggle_fullscreen();
                } else if (event.key.key == SDLK_L) {
                    toggle_frame_rate_lock();
                } else if (event.key.key == SDLK_MINUS || event.key.key == SDLK_KP_MINUS) {
                    step_window_scale(-1);
                } else if (event.key.key == SDLK_EQUALS || event.key.key == SDLK_PLUS ||
                           event.key.key == SDLK_KP_EQUALS || event.key.key == SDLK_KP_PLUS) {
                    step_window_scale(1);
                }
                break;
            case SDL_EVENT_TEXT_INPUT:
                // A key bound to a control is a control, not a letter: SDL sends the key press
                // before the text it produces, so the press that was just used swallows it.
                if (swallow_next_text_) {
                    swallow_next_text_ = false;
                } else {
                    input.typed.characters += event.text.text;
                }
                break;
            default:
                break;
            }
        }
        input.touch = touched_side();
        input.spin = spin_direction();
        service_audio();
    }

    [[nodiscard]] bool text_input_supported() const override { return true; }

    void present(const uint8_t* rgb, unsigned width, unsigned height) override {
        if (renderer_ == nullptr || !ensure_texture(width, height)) {
            return;
        }
        SDL_UpdateTexture(texture_, nullptr, rgb, static_cast<int>(width) * 3);
        SDL_RenderClear(renderer_);

        // Is the picture bigger than the window it has to fit in?
        //
        // It never used to be able to be. The game drew 320x240 and every window was larger, so
        // the last step was always a magnification, and `Nearest` — whole, hard pixel blocks —
        // was the right filter for it. A render scale above 1 breaks that assumption: 4x is
        // 1280x960 and the default window is 960x720, so the last step can now be a *reduction*,
        // and the two want opposite filters. Reducing with `Nearest` keeps three pixels out of
        // four and throws the rest away, which costs exactly the fine dark detail.
        //
        // On the Sharp path this was already half-right by accident: its second pass is a linear
        // fit, so a reduction of less than two was already filtered, and at 4x this branch draws
        // the same window as the code it replaced, pixel for pixel. What it fixes is `Nearest`
        // scaling, which had no second pass at all, and every reduction of more than two — 8x
        // into a small window is 2.67x, which one bilinear tap cannot do honestly.
        int output_width = 0, output_height = 0;
        const bool have_output =
            SDL_GetCurrentRenderOutputSize(renderer_, &output_width, &output_height);
        const bool reducing =
            have_output && (texture_width_ > output_width || texture_height_ > output_height);

        SDL_Texture* source = texture_;
        if (reducing) {
            // Reduced, so filtered — and by halves, because one bilinear tap reads four texels
            // and can only honestly reduce by two. 8x into a small window is three halvings and
            // then the fit; each step is a proper average of what it replaces, which is what
            // makes the extra pixels worth having: drawn at 4x and shown at 1x, the picture is
            // supersampled, which is the one thing the iPod's own could never be.
            SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_LINEAR);
            source = reduce_to_fit(output_width, output_height);
        } else {
            apply_texture_scale_mode();
            // Sharp scaling is two passes: whole-number blocks first, so every game pixel is the
            // same size, then a smooth fit of that to the window, which only ever has to soften
            // the fraction left over. One pass of either alone gives uneven blocks or a blurred
            // picture. It is a magnification and only makes sense as one, which is why it sits on
            // this side of the branch.
            if (settings().scaling == Scaling::Sharp && update_prescale()) {
                SDL_SetRenderTarget(renderer_, prescale_);
                SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
                SDL_SetRenderTarget(renderer_, nullptr);
                source = prescale_;
            }
        }
        SDL_RenderTexture(renderer_, source, nullptr, nullptr);
        SDL_RenderPresent(renderer_);
        update_frame_rate_display();
    }

    void wait_for_next_frame() override {
        // Where the display is doing the pacing on its own, `present` has already waited the
        // whole frame and a sleep on top would only make it late. See `apply_frame_pacing`.
        if (vsync_paces_) {
            return;
        }
        const Uint64 interval_ns = frame_interval_ns();
        if (interval_ns == 0) {
            return;
        }
        next_frame_ns_ += interval_ns;
        const Uint64 now = SDL_GetTicksNS();
        if (next_frame_ns_ > now) {
            SDL_DelayNS(next_frame_ns_ - now);
        } else if (now - next_frame_ns_ > interval_ns * 4) {
            next_frame_ns_ = now;  // fell far behind (window dragged, machine busy): don't catch up
        }
    }

    // Wait for the display before showing a frame, so that a frame is never half of one and
    // half of the next.
    //
    // Presenting whenever a timer expires means presenting in the middle of a refresh, and the
    // seam between the two frames is a tear. It is worst at a rate that is not the display's: at
    // 30 frames a second on a 120 Hz screen the two drift against each other, so the tear line
    // marches down the picture instead of sitting still.
    //
    // Two ways to wait, and the better one is not always available. A vsync *interval* — present
    // every Nth refresh — paces the frames exactly and needs no timer at all, but macOS's Metal
    // renderer takes only 1 and refuses anything above it. So the interval is asked for first and
    // plain vsync is the fallback: presenting still waits for a refresh, which is what stops the
    // tearing, and the timer below decides which refresh to land on. Running unlocked asks for
    // frames as fast as they come, which means no waiting and a torn picture by choice.
    void apply_frame_pacing() {
        vsync_paces_ = false;
        if (renderer_ == nullptr) {
            return;
        }
        const unsigned rate = settings().frame_rate;
        const float refresh = display_refresh_hz();
        int interval = 0;
        if (rate != 0) {
            interval = 1;
            if (refresh > 0.0f) {
                const float refreshes = refresh / static_cast<float>(rate);
                const int whole = static_cast<int>(std::lround(refreshes));
                if (whole >= 1 && std::fabs(refreshes - static_cast<float>(whole)) < 0.01f &&
                    SDL_SetRenderVSync(renderer_, whole)) {
                    vsync_paces_ = true;
                    interval = whole;
                }
            }
        }
        if (!vsync_paces_ &&
            !SDL_SetRenderVSync(renderer_, interval == 0 ? SDL_RENDERER_VSYNC_DISABLED : 1)) {
            std::fprintf(stderr,
                         "display: renderer %s refuses vsync (%s) — the picture will tear\n",
                         SDL_GetRendererName(renderer_), SDL_GetError());
            interval = 0;
        }
        report_frame_pacing(rate, refresh, interval);
        next_frame_ns_ = SDL_GetTicksNS();
    }

    // Said once per change rather than per frame, because "why does it tear" is a question this
    // line answers, and guessing at it is what cost the afternoon it was written in.
    void report_frame_pacing(unsigned rate, float refresh, int interval) {
        if (interval == reported_interval_ && rate == reported_rate_) {
            return;
        }
        reported_interval_ = interval;
        reported_rate_ = rate;
        if (rate == 0) {
            std::printf("display: unlocked — frames as fast as they come, and it will tear\n");
        } else if (vsync_paces_) {
            std::printf("display: %.4g Hz, %u fps — every %d refresh%s, paced by the display\n",
                        static_cast<double>(refresh), rate, interval, interval == 1 ? "" : "es");
        } else if (interval != 0) {
            std::printf("display: %.4g Hz, %u fps — waits for a refresh, paced by a timer\n",
                        static_cast<double>(refresh), rate);
        }
    }

    // What the display the window is on refreshes at, or 0 if it will not say.
    [[nodiscard]] float display_refresh_hz() const {
        const SDL_DisplayID display = SDL_GetDisplayForWindow(window_);
        const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(display);
        return mode == nullptr ? 0.0f : mode->refresh_rate;
    }

    void play_sound(uint32_t voice, const SoundClip& clip, bool looping) override {
        // The voice already playing this sound takes it again — a retrigger restarts it — and
        // otherwise any idle one will do. All of them busy means the device's polyphony is used
        // up, as it was on the iPod.
        Voice* chosen = nullptr;
        for (Voice& candidate : voices_) {
            if (!candidate.idle() && candidate.sounding() == voice) {
                chosen = &candidate;
                break;
            }
            if (chosen == nullptr && candidate.idle()) {
                chosen = &candidate;
            }
        }
        if (chosen != nullptr) {
            (void)chosen->start(voice, clip, looping);
        }
    }

    void stop_sound(uint32_t voice) override {
        for (Voice& candidate : voices_) {
            if (!candidate.idle() && candidate.sounding() == voice) {
                candidate.stop();
            }
        }
    }

    void play_music(const std::string& path, bool repeat) override { music_.play(path, repeat); }

    // SDL's dialog answers through a callback from the event loop, so pump events until it
    // does. The answer is shared, not a local: SDL calls back when the dialog closes, which may
    // be after the player has quit and this function has returned. Both sides hold a reference,
    // so whichever finishes last releases it.
    bool choose_file(const std::string& prompt, const std::string& extension,
                     std::string& chosen_path) override {
        const auto answer = std::make_shared<Answer>();
        const SDL_DialogFileFilter filters[] = {{prompt.c_str(), extension.c_str()}};
        SDL_ShowOpenFileDialog(on_dialog_answer, new std::shared_ptr<Answer>(answer), window_,
                               filters, 1, nullptr, false);
        return wait_for_dialog(*answer, chosen_path);
    }

    bool choose_directory(const std::string& prompt, std::string& chosen_path) override {
        // SDL's folder dialog takes no filter, so the prompt has nowhere to go — the window
        // title is the system's. It is still worth printing: a dialog that appears with no
        // explanation is the same fault as no dialog at all.
        std::printf("%s\n", prompt.c_str());
        const auto answer = std::make_shared<Answer>();
        SDL_ShowOpenFolderDialog(on_dialog_answer, new std::shared_ptr<Answer>(answer), window_,
                                 nullptr, false);
        return wait_for_dialog(*answer, chosen_path);
    }

private:
    // Declared before every other member, so that it is destroyed after every other member: SDL
    // outlives everything of SDL's that this class holds. See `SdlSession`.
    SdlSession sdl_session_;

    // What a file or folder dialog reports back. `done` is written from SDL's callback, which
    // may run on another thread, and read by `wait_for_dialog`.
    struct Answer {
        std::atomic<bool> done{false};
        std::string path;
    };

    // SDL hands the callback the `userdata` it was given; that is a reference this class made
    // over to it, and dropping it here is what keeps the answer alive until the callback comes
    // even if the caller has given up waiting.
    static void on_dialog_answer(void* userdata, const char* const* paths, int /*filter*/) {
        const std::unique_ptr<std::shared_ptr<Answer>> held(
            static_cast<std::shared_ptr<Answer>*>(userdata));
        Answer& result = **held;
        if (paths != nullptr && paths[0] != nullptr) {
            result.path = paths[0];
        }
        result.done.store(true, std::memory_order_release);
    }

    // Pump events until the dialog answers. Closing the window while it is open gives up on it —
    // the answer object outlives this call, so the callback still has somewhere to land.
    bool wait_for_dialog(Answer& answer, std::string& chosen_path) {
        SDL_Event event;
        while (!answer.done.load(std::memory_order_acquire)) {
            if (SDL_WaitEventTimeout(&event, 50) && event.type == SDL_EVENT_QUIT) {
                return false;
            }
        }
        chosen_path = answer.path;
        return !chosen_path.empty();
    }

    // The keys a settings window may offer. Naming them here rather than asking the player to
    // press one keeps the window out of this platform's event handling entirely: it only has to
    // show a list. SDL names each key for us, so the labels match what the keyboard says.
    static const InputChoice* assignable_keys() {
        static InputChoice keys[ASSIGNABLE_KEY_COUNT];
        static bool built = false;
        if (!built) {
            built = true;
            static const SDL_Keycode codes[ASSIGNABLE_KEY_COUNT] = {
                SDLK_ESCAPE,      SDLK_LEFT,
                SDLK_RIGHT,       SDLK_UP,
                SDLK_DOWN,        SDLK_SPACE,
                SDLK_RETURN,      SDLK_TAB,
                SDLK_COMMA,       SDLK_PERIOD,
                SDLK_SLASH,       SDLK_SEMICOLON,
                SDLK_LEFTBRACKET, SDLK_RIGHTBRACKET,
                SDLK_MINUS,       SDLK_EQUALS,
                SDLK_A,           SDLK_B,
                SDLK_C,           SDLK_D,
                SDLK_E,           SDLK_G,
                SDLK_H,           SDLK_I,
                SDLK_J,           SDLK_K,
                SDLK_M,           SDLK_N,
                SDLK_O,           SDLK_R,
                SDLK_S,           SDLK_T,
                SDLK_U,           SDLK_V,
                SDLK_W,           SDLK_X,
                SDLK_Y,           SDLK_Z,
                SDLK_1,           SDLK_2,
                SDLK_3,           SDLK_4,
                SDLK_5,           SDLK_6,
                SDLK_7,           SDLK_8,
                SDLK_9,           SDLK_0,
            };
            static_assert(sizeof(codes) / sizeof(codes[0]) == ASSIGNABLE_KEY_COUNT,
                          "ASSIGNABLE_KEY_COUNT must match the list");
            for (unsigned i = 0; i < ASSIGNABLE_KEY_COUNT; ++i) {
                keys[i].code = static_cast<InputCode>(codes[i]);
                keys[i].label = SDL_GetKeyName(codes[i]);  // SDL owns the string; it is static
            }
        }
        return keys;
    }

    // Which of the device's five buttons an action presses. The wheel is handled before this,
    // because turning it repeats while a button press does not.
    // Which side of the wheel an action rests a finger on, if it is one of those.
    static WheelTouch side_for(Action action) {
        switch (action) {
        case Action::TouchUp:
            return WheelTouch::Top;
        case Action::TouchRight:
            return WheelTouch::Right;
        case Action::TouchBottom:
            return WheelTouch::Bottom;
        case Action::TouchLeft:
            return WheelTouch::Left;
        default:
            return WheelTouch::None;
        }
    }

    // What the player's finger is on right now. Read from the keyboard's *state* rather than
    // from key events, because this is the one control that is held rather than pressed: the
    // game walks for as long as it lasts. Two sides at once is not a thing a finger does, so the
    // first in this order wins.
    static WheelTouch touched_side() {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        if (keys == nullptr) {
            return WheelTouch::None;
        }
        for (const Action action :
             {Action::TouchUp, Action::TouchRight, Action::TouchBottom, Action::TouchLeft}) {
            for (unsigned slot = 0; slot < BINDING_SLOTS; ++slot) {
                const InputCode code = input_bindings().code(action, slot);
                if (code == NO_INPUT) {
                    continue;
                }
                const SDL_Scancode scancode =
                    SDL_GetScancodeFromKey(static_cast<SDL_Keycode>(code), nullptr);
                if (scancode != SDL_SCANCODE_UNKNOWN && keys[scancode]) {
                    return side_for(action);
                }
            }
        }
        return WheelTouch::None;
    }

    // Which way the wheel is being turned right now, from the keyboard's *state*.
    //
    // A held scroll key is a finger going round the wheel and not letting go, which is a
    // different thing from a press: the game watches the angle *change while contact lasts*, and
    // an eight-detent flick that ends in the queue running dry is a finger lifted. Anything that
    // asks for a sustained turn never sees one.
    static int spin_direction() {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        if (keys == nullptr) {
            return 0;
        }
        const auto held = [keys](Action action) {
            for (unsigned slot = 0; slot < BINDING_SLOTS; ++slot) {
                const InputCode code = input_bindings().code(action, slot);
                if (code == NO_INPUT) {
                    continue;
                }
                const SDL_Scancode scancode =
                    SDL_GetScancodeFromKey(static_cast<SDL_Keycode>(code), nullptr);
                if (scancode != SDL_SCANCODE_UNKNOWN && keys[scancode]) {
                    return true;
                }
            }
            return false;
        };
        // Both at once is no rotation at all, which is what two fingers pushing opposite ways
        // would do.
        return static_cast<int>(held(Action::SwipeRight)) -
               static_cast<int>(held(Action::SwipeLeft));
    }

    static uint32_t button_for(Action action) {
        switch (action) {
        case Action::Select:
            return static_cast<uint32_t>(Button::Select);
        case Action::PlayPause:
            return static_cast<uint32_t>(Button::Play);
        case Action::Menu:
            return static_cast<uint32_t>(Button::Menu);
        case Action::Rewind:
            return static_cast<uint32_t>(Button::Previous);
        case Action::FastForward:
            return static_cast<uint32_t>(Button::Next);
        default:
            return 0;
        }
    }

    // A key press, through whatever the player has bound it to. The two keys that are not the
    // device's — a screenshot and quitting — are fixed, because they are the program's rather
    // than the game's.
    // True when the key did something, so the caller knows not to offer it to anything else.
    static bool handle_key(SDL_Keycode key, bool repeat, FrameInput& input) {
        Action action = Action::Select;
        const bool bound = input_bindings().action_for(static_cast<InputCode>(key), action);
        if (trace_input() && !repeat) {
            std::fprintf(stderr, "key 0x%x (%s) %s\n", static_cast<unsigned>(key),
                         input_label(static_cast<InputCode>(key)),
                         bound ? action_label(action) : "not bound");
        }
        if (bound) {
            // A *press* of a scroll key is a flick worth one row. Holding it is a different
            // gesture — see `spin_direction` — so the repeats that follow are left alone.
            if (action == Action::SwipeLeft) {
                if (!repeat) {
                    input.wheel_detents -= DETENTS_PER_ROW;
                }
                return true;
            }
            if (action == Action::SwipeRight) {
                if (!repeat) {
                    input.wheel_detents += DETENTS_PER_ROW;
                }
                return true;
            }
            if (side_for(action) != WheelTouch::None) {
                return true;  // a level, read from the keyboard in `poll` rather than from events
            }
            if (!repeat) {  // a held button is one press
                input.buttons |= button_for(action);
            }
            return true;
        }
        if (repeat) {
            return false;
        }
        switch (key) {
        case SDLK_P:
            input.screenshot = true;
            return true;
        case SDLK_Q:
            input.quit = true;
            return true;
        default:
            return false;
        }
    }

    // Grow or shrink the window by one whole multiple of the screen, between 1x and MAX_PRESCALE.
    // Whole multiples are where the picture looks its best whatever the scaling, and the window's
    // locked shape means the step is the same in both directions.
    void step_window_scale(int by) {
        int width = 0, height = 0;
        SDL_GetWindowSize(window_, &width, &height);
        const int now = std::max(1, (width + static_cast<int>(SCREEN_WIDTH) / 2) /
                                        static_cast<int>(SCREEN_WIDTH));
        const int wanted = std::clamp(now + by, 1, MAX_PRESCALE);
        SDL_SetWindowSize(window_, static_cast<int>(SCREEN_WIDTH) * wanted,
                          static_cast<int>(SCREEN_HEIGHT) * wanted);
    }

    // With "whole multiples" on, the *window* takes a whole multiple of 320x240 rather than the
    // picture taking one inside a larger window. Same exact pixel blocks, no border.
    //
    // Nothing to do in full screen: the size is the display's. And nothing to do while a snap of
    // our own is still being delivered, or each resize would arrive back here and set the size
    // again.
    void snap_window_to_whole_multiple() {
        if (!settings().pixel_perfect || fullscreen_ || snapping_) {
            return;
        }
        int width = 0, height = 0;
        SDL_GetWindowSize(window_, &width, &height);
        const int wanted = std::clamp((width + static_cast<int>(SCREEN_WIDTH) / 2) /
                                          static_cast<int>(SCREEN_WIDTH),
                                      1, MAX_PRESCALE);
        const int snapped_width = static_cast<int>(SCREEN_WIDTH) * wanted;
        const int snapped_height = static_cast<int>(SCREEN_HEIGHT) * wanted;
        if (width == snapped_width && height == snapped_height) {
            return;
        }
        snapping_ = true;
        SDL_SetWindowSize(window_, snapped_width, snapped_height);
        snapping_ = false;
    }

    void toggle_fullscreen() {
        fullscreen_ = !fullscreen_;
        if (!SDL_SetWindowFullscreen(window_, fullscreen_)) {
            std::fprintf(stderr, "cannot change full screen: %s\n", SDL_GetError());
            fullscreen_ = !fullscreen_;
        }
        // Which of the two ways "whole multiples" is kept depends on which of these we are in.
        apply_presentation();
        snap_window_to_whole_multiple();
    }

    // How long a frame is meant to take, or 0 when the rate is unlocked and it may take as long
    // as it likes.
    [[nodiscard]] Uint64 frame_interval_ns() const {
        const unsigned rate = settings().frame_rate;
        return rate == 0 ? 0 : 1'000'000'000ull / rate;
    }

    // Frames are paced to `rate` a second; 0 runs as fast as the machine allows, which
    // fast-forwards the game. Settings ▸ General and the L key both come through here.
    void set_frame_rate(unsigned rate) {
        settings().frame_rate = rate;
        if (rate != 0) {
            paced_rate_ = rate;  // what L goes back to
        }
        apply_frame_pacing();  // also paces from now, not from where the unlocked run left off
        macos_settings_set_frame_rate(rate);
        save_settings();
    }

    // L, the shortcut for the two rates a player switches between while testing.
    void toggle_frame_rate_lock() { set_frame_rate(settings().frame_rate == 0 ? paced_rate_ : 0); }

    // How the picture reaches the window: the filter, and whether it may be scaled by a fraction
    // at all. Settings ▸ Graphics comes through here.
    void set_scaling(Scaling scaling) {
        settings().scaling = scaling;
        apply_presentation();
        save_settings();
    }

    void set_pixel_perfect(bool pixel_perfect) {
        settings().pixel_perfect = pixel_perfect;
        apply_presentation();
        snap_window_to_whole_multiple();  // asking for it takes effect now, not on the next drag
        save_settings();
    }

    // The three that are the renderer's business rather than the window's. Each one only records
    // the choice and saves it: the frame pump reads `settings()` at the top of every frame and
    // hands the renderer whatever it now says (src/runtime/main.cpp), so there is nothing to
    // apply here and no second copy of the answer to keep in step.
    void set_render_scale(unsigned scale) {
        settings().render_scale = std::clamp(scale, MIN_RENDER_SCALE, MAX_RENDER_SCALE);
        save_settings();
    }

    void set_high_resolution_text(bool resolve) {
        settings().high_resolution_text = resolve;
        save_settings();
    }

    void set_unlock_all_chapters(bool unlock) {
        settings().unlock_all_chapters = unlock;
        save_settings();
    }

    // Everything at once, for the settings read from the store at start-up.
    void apply_settings() override {
        paced_rate_ = settings().frame_rate == 0 ? paced_rate_ : settings().frame_rate;
        apply_frame_pacing();
        apply_presentation();
        set_title_now();
        macos_settings_set_frame_rate(settings().frame_rate);
    }

    void apply_presentation() {
        // The picture fills the window. The window is locked to the screen's shape, so filling it
        // costs no distortion and there is nothing left over to letterbox.
        //
        // "Whole multiples" used to be enforced *here*, by refusing a fractional size: the picture
        // took whatever whole multiple fitted and the rest of the window was border, so the margin
        // grew and shrank as the window was dragged and the game never filled it. It is enforced
        // on the window instead now — see snap_window_to_whole_multiple — which gets the same
        // exact pixel blocks with no border at all.
        //
        // Full screen is the exception, because there the size is the display's and not ours to
        // round. That is the one place the old behaviour is still the only way to honour the
        // setting, and the border there is the display's shape, not a choice.
        SDL_SetRenderLogicalPresentation(
            renderer_, static_cast<int>(SCREEN_WIDTH), static_cast<int>(SCREEN_HEIGHT),
            settings().pixel_perfect && fullscreen_ ? SDL_LOGICAL_PRESENTATION_INTEGER_SCALE
                                                    : SDL_LOGICAL_PRESENTATION_LETTERBOX);
        apply_texture_scale_mode();
    }

    // The intermediate texture Sharp draws through: the game's picture at the smallest whole
    // multiple that covers the window, so the smooth pass never has to enlarge, only shrink a
    // little. Rebuilt when the window size asks for a different multiple. False if it cannot be
    // had, and the caller falls back to one plain pass.
    bool update_prescale() {
        int output_width = 0, output_height = 0;
        if (!SDL_GetCurrentRenderOutputSize(renderer_, &output_width, &output_height)) {
            return false;
        }
        // Whole multiples of *what the renderer handed over*, which is 320x240 only at render
        // scale 1. Measuring against the game's own size instead would prescale an already
        // enlarged picture by the same factor again — four times the texture for a picture that
        // was already big enough, and on a 4K display more than a renderer will allocate.
        const int wanted =
            std::clamp(std::max((output_width + texture_width_ - 1) / texture_width_,
                                (output_height + texture_height_ - 1) / texture_height_),
                       1, MAX_PRESCALE);
        if (prescale_ != nullptr && prescale_factor_ == wanted &&
            prescale_source_width_ == texture_width_) {
            return true;
        }
        if (prescale_ != nullptr) {
            SDL_DestroyTexture(prescale_);
        }
        prescale_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_TARGET,
                                      texture_width_ * wanted, texture_height_ * wanted);
        prescale_factor_ = prescale_ == nullptr ? 0 : wanted;
        prescale_source_width_ = texture_width_;
        if (prescale_ == nullptr) {
            return false;
        }
        SDL_SetTextureScaleMode(prescale_, SDL_SCALEMODE_LINEAR);
        return true;
    }

    // Halve the picture until it is no more than twice the output in each direction, and answer
    // whatever the last step left it in. Two targets, used alternately, because a texture cannot
    // be read and written in the same pass; both are rebuilt only when the size they need
    // changes, so a steady window allocates nothing per frame.
    //
    // Answering `texture_` when there is nothing to do is the common case — a window between one
    // and two times the picture — and costs one comparison.
    SDL_Texture* reduce_to_fit(int output_width, int output_height) {
        SDL_Texture* source = texture_;
        int source_width = texture_width_, source_height = texture_height_;
        unsigned step = 0;
        while (source_width / 2 >= output_width && source_height / 2 >= output_height &&
               step < MAX_REDUCTION_STEPS) {
            const int half_width = source_width / 2, half_height = source_height / 2;
            SDL_Texture*& target = reduce_[step % 2];
            if (target == nullptr || reduce_size_[step % 2].first != half_width ||
                reduce_size_[step % 2].second != half_height) {
                if (target != nullptr) {
                    SDL_DestroyTexture(target);
                }
                target = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB24,
                                           SDL_TEXTUREACCESS_TARGET, half_width, half_height);
                if (target == nullptr) {
                    return source;  // nothing to reduce into; the fit below still filters
                }
                SDL_SetTextureScaleMode(target, SDL_SCALEMODE_LINEAR);
                reduce_size_[step % 2] = {half_width, half_height};
            }
            SDL_SetRenderTarget(renderer_, target);
            SDL_RenderTexture(renderer_, source, nullptr, nullptr);
            SDL_SetRenderTarget(renderer_, nullptr);
            source = target;
            source_width = half_width;
            source_height = half_height;
            ++step;
        }
        return source;
    }

    // The streaming texture the game's picture is uploaded through, at whatever size the renderer
    // is drawing. Rebuilt when that size changes, which is when the render scale does.
    bool ensure_texture(unsigned width, unsigned height) {
        if (width == 0 || height == 0) {
            return false;
        }
        if (texture_ != nullptr && texture_width_ == static_cast<int>(width) &&
            texture_height_ == static_cast<int>(height)) {
            return true;
        }
        if (texture_ != nullptr) {
            SDL_DestroyTexture(texture_);
        }
        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
                                     static_cast<int>(width), static_cast<int>(height));
        if (texture_ == nullptr) {
            texture_width_ = texture_height_ = 0;
            return false;
        }
        texture_width_ = static_cast<int>(width);
        texture_height_ = static_cast<int>(height);
        apply_texture_scale_mode();
        return true;
    }

    void apply_texture_scale_mode() {
        if (texture_ == nullptr) {
            return;
        }
        // Sharp does its own smoothing in the second pass; the game's own picture is always
        // magnified by whole blocks.
        SDL_SetTextureScaleMode(texture_, settings().scaling == Scaling::Smooth
                                              ? SDL_SCALEMODE_LINEAR
                                              : SDL_SCALEMODE_NEAREST);
    }

    void set_show_frame_rate(bool show) {
        settings().show_frame_rate = show;
        set_title_now();
        save_settings();
    }

    // The plain title, and a fresh start for the frame counter behind the rate.
    void set_title_now() {
        if (!settings().show_frame_rate) {
            SDL_SetWindowTitle(window_, title_.c_str());
        }
        title_updated_ns_ = SDL_GetTicksNS();
        frames_since_title_ = 0;
    }

    // The window title carries the live frame rate, refreshed twice a second, for as long as the
    // player wants to see it (Settings ▸ General).
    void update_frame_rate_display() {
        if (!settings().show_frame_rate) {
            return;
        }
        ++frames_since_title_;
        const Uint64 now = SDL_GetTicksNS();
        if (now - title_updated_ns_ < TITLE_REFRESH_NS) {
            return;
        }
        const double seconds = static_cast<double>(now - title_updated_ns_) / 1e9;
        const double fps = static_cast<double>(frames_since_title_) / seconds;
        char title[96];
        std::snprintf(title, sizeof title, "%s — %.1f fps%s", title_.c_str(), fps,
                      settings().frame_rate == 0 ? " (unlocked)" : "");
        SDL_SetWindowTitle(window_, title);
        title_updated_ns_ = now;
        frames_since_title_ = 0;
    }

    void service_audio() {
        for (Voice& voice : voices_) {
            voice.service();
        }
        music_.service();
    }

    std::string title_;
    Uint64 title_updated_ns_ = 0;
    unsigned frames_since_title_ = 0;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    int texture_width_ = 0;
    int texture_height_ = 0;
    int prescale_source_width_ = 0;
    SDL_Texture* reduce_[2] = {nullptr, nullptr};
    std::pair<int, int> reduce_size_[2] = {{0, 0}, {0, 0}};
    bool fullscreen_ = false;
    // Set while a snap of our own is in flight, so the resize it causes is not snapped again.
    bool snapping_ = false;
    unsigned paced_rate_;              // the rate L goes back to from unlocked
    SDL_Texture* prescale_ = nullptr;  // Sharp's whole-number intermediate
    int prescale_factor_ = 0;

    Uint64 next_frame_ns_ = 0;
    // Whether the vsync interval is pacing the frames by itself. False when the renderer would
    // not take an interval above 1 and the timer in `wait_for_next_frame` is doing it instead.
    bool vsync_paces_ = false;
    int reported_interval_ = -1;  // what was last reported, so it is said only on a change
    unsigned reported_rate_ = 0;
    bool swallow_next_text_ = false;  // the press that produced this text was a control
    Voice voices_[VOICE_LIMIT];       // opened as they are first needed, then kept
    MusicPlayer music_;
};

}  // namespace

std::unique_ptr<Platform> create_platform(const char* window_title, unsigned frames_per_second) {
    return std::make_unique<Sdl3Platform>(window_title, frames_per_second);
}

}  // namespace holdem::platform
