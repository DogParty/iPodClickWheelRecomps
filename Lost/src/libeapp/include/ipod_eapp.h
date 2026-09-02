// libeapp — the iPod's application frameworks, reimplemented on the host.
//
// An eApp calls the firmware through numbered import thunks grouped into frameworks (OpenGLES,
// Audio, AsyncFileIO, InputEvents, miscTBD, Settings). This library is that surface: the
// game-facing side of it is the typed interfaces in `src/framework/` (graphics, audio, storage,
// controls, device), and the files here implement them the way the hardware behaved — each
// established in the emulator (reference/eapp-loader/lib.rs, the `Stub` enum) and cited where it
// is implemented. `src/libeapp/imports.json` records which framework ordinal each one is.
//
// This header is the rest of the library: the services the runtime and the platform layer use,
// and the call log the oracle compares.
#pragma once

#include "runtime/cpu.h"

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

namespace lost::eapp {

// ---------------------------------------------------------------------------------------------
// The call log: one line per framework call, identical in format to the emulator's, so the two
// can be diffed. Owned by the runtime's main loop, which tells it the current frame number.
// ---------------------------------------------------------------------------------------------

class CallLog {
public:
    // Start writing to `path` ("-" for stdout). Fatal if the file cannot be created.
    void open(const char* path);
    void begin_frame(unsigned frame) { frame_ = frame; }
    void record(const Cpu& cpu, const char* framework, unsigned ordinal);
    void record(const char* framework, unsigned ordinal, std::initializer_list<uint32_t> given);
    [[nodiscard]] bool enabled() const { return file_ != nullptr; }
    ~CallLog();

private:
    void write(const char* framework, unsigned ordinal, const uint32_t (&arguments)[8],
               uint32_t return_address);

    std::FILE* file_ = nullptr;
    unsigned frame_ = 0;
};

CallLog& call_log();

// Record one framework call. Every typed entry point begins with this; it names the ordinal the
// hardware knew the call by, and its arguments in order.
void log_call(const char* framework, unsigned ordinal, std::initializer_list<uint32_t> arguments);

// ---------------------------------------------------------------------------------------------
// The ARM entry point, for the pure recompilation only
// ---------------------------------------------------------------------------------------------
//
// Recompiled ARM code calls the frameworks the way the hardware did: arguments in r0-r3 and on
// the stack, result in r0. `src/libeapp/arm_abi.cpp` unpacks each one and calls the typed entry
// point behind it. Nothing in the hand-decompiled game goes through here.

using Implementation = void (*)(Cpu&);

void framework_call(Cpu& cpu, const char* framework, unsigned ordinal,
                    Implementation implementation);

// The adapters themselves, one per ordinal, named as `src/libeapp/imports.json` names them
// because `tools/emit.py` generates calls to these names.
// miscTBD and Settings
void misc_alloc(Cpu& cpu);
void misc_clock(Cpu& cpu);
void misc_device_level_get(Cpu& cpu);
void misc_device_level_set(Cpu& cpu);
void misc_free(Cpu& cpu);
void misc_host_battery(Cpu& cpu);
void misc_host_time(Cpu& cpu);
void misc_resolve_name(Cpu& cpu);
void misc_thousand(Cpu& cpu);
void misc_zero(Cpu& cpu);
void settings_get(Cpu& cpu);

// InputEvents
void input_poll(Cpu& cpu);

// AsyncFileIO
void afio_close(Cpu& cpu);
void afio_open(Cpu& cpu);
void afio_open_alt(Cpu& cpu);
void afio_operation(Cpu& cpu);
void afio_read(Cpu& cpu);
void afio_store_open(Cpu& cpu);
void afio_store_close(Cpu& cpu);
void afio_store_write(Cpu& cpu);

// Audio
void audio_query_ff(Cpu& cpu);
void audio_sfx_create(Cpu& cpu);
void audio_sfx_get_field_04(Cpu& cpu);
void audio_sfx_play(Cpu& cpu);
void audio_sfx_release(Cpu& cpu);
void audio_sfx_set_buffer(Cpu& cpu);
void audio_sfx_set_field_08(Cpu& cpu);
void audio_sfx_set_field_0c(Cpu& cpu);
void audio_sfx_set_field_10(Cpu& cpu);
void audio_sfx_set_field_14(Cpu& cpu);
void audio_sfx_set_field_18(Cpu& cpu);
void audio_sfx_set_field_1c(Cpu& cpu);
void audio_sfx_set_field_20(Cpu& cpu);
void audio_sfx_set_field_24(Cpu& cpu);
void audio_sfx_set_field_3e(Cpu& cpu);
void audio_sfx_set_state(Cpu& cpu);
void audio_stream_play(Cpu& cpu);
void audio_stream_register(Cpu& cpu);
void audio_stream_repeat(Cpu& cpu);
void audio_zero(Cpu& cpu);

// Metadata
void metadata_now_playing_playlist(Cpu& cpu);
void metadata_playlist_track_count(Cpu& cpu);

// OpenGLES
void gl_active_texture(Cpu& cpu);
void gl_bind_texture(Cpu& cpu);
void gl_clear(Cpu& cpu);
void gl_clear_color(Cpu& cpu);
void gl_compressed_tex_image_2d(Cpu& cpu);
void gl_copy_tex_image_2d(Cpu& cpu);
void gl_disable_vertex_attrib_array(Cpu& cpu);
void gl_draw_arrays(Cpu& cpu);
void gl_enable_vertex_attrib_array(Cpu& cpu);
void gl_load_identity(Cpu& cpu);
void gl_mult_matrix(Cpu& cpu);
void gl_one(Cpu& cpu);
void gl_ortho(Cpu& cpu);
void gl_pixel_store(Cpu& cpu);
void gl_query_0x3000(Cpu& cpu);
void gl_set_render_server_image(Cpu& cpu);
void gl_start_render_server(Cpu& cpu);
void gl_stop_render_server(Cpu& cpu);
void gl_swap_buffers(Cpu& cpu);
void gl_tex_image_2d(Cpu& cpu);
void gl_uniform_4x_scalar(Cpu& cpu);
void gl_uniform_matrix_4fv(Cpu& cpu);
void gl_uniform_matrix_4xv(Cpu& cpu);
void gl_vertex_attrib_pointer(Cpu& cpu);
void gl_zero(Cpu& cpu);

// ---------------------------------------------------------------------------------------------
// Host-side state the frame pump feeds and the frameworks consume
// ---------------------------------------------------------------------------------------------

// Click-wheel samples waiting for the game to poll them. The pump queues one event per detent
// and keeps one sample in flight every frame; the poll consumes one per call. The low byte is
// the wheel position code, bit 30 is "event present" (lib.rs `queue_input`).
void queue_input(uint8_t code);

// Throw away whatever is still queued. A finger resting on the wheel is a *steady position*, so
// the samples behind it are stale — left there, the game reads the difference as rotation.
void clear_input_queue();

// File completions the host owes the game. A storage call parks the request here; the pump runs
// the request's callback between frames (play.rs "Deliver any completion").
// `take_pending_completions` hands back everything queued so far and clears the queue.
[[nodiscard]] std::vector<uint32_t> take_pending_completions();

// The resource directory the file framework serves from; set once by the runtime at start.
void set_game_directory(const std::string& path);

// Make the wall clock report this time of day (24-hour clock) instead of the real one, so a
// scripted run draws the same clock digits whenever it is replayed.
void set_fixed_host_time(int hour, int minute);

// Report the clock and the battery as the *emulator's* stubs did — an hour already folded to 12,
// a call that answers 0, a charge pinned full — which is what every recording in tests/expected/
// was made against. `--emulator-firmware` asks for it; a real run wants the truth.
void set_emulator_device(bool emulator);

// Sound effects and music the game asked for since the last call, for the platform to play.
//
// A sound effect arrives as *samples*, not as a file name, because that is how the game gives it:
// it reads its own `soundbank_*.dat` — a table of names and formats followed by the raw PCM — and
// hands the platform a buffer and the four numbers needed to read it (`Audio #7` the buffer, #8
// its length, #10 the rate, #11 the channels, #12 the bits). There is no `.wav` anywhere in the
// game's files, and nothing here has to know the bank format.
enum class SoundAction { Play, PlayLooping, Stop };
struct SoundRequest {
    uint32_t voice;  // the game's own handle: what a Stop names, and what a retrigger matches
    SoundAction action;
    std::vector<uint8_t> samples;  // raw PCM, in the format below; empty for a Stop
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t bits;  // 8 (unsigned) or 16 (signed), the two the hardware took
};
struct MusicRequest {
    std::string file;  // resolved resource name, relative to the game directory
    bool repeat;
};
[[nodiscard]] std::vector<SoundRequest> take_sound_requests();
[[nodiscard]] std::vector<MusicRequest> take_music_requests();

}  // namespace lost::eapp
