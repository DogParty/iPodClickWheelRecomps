// The Audio framework: sound-effect descriptors and music streams.
//
// Two mechanisms live behind one framework. Sound effects: #0 creates a descriptor and returns
// its handle, #7 points it at a PCM buffer the game loaded from a `.wav`, #2 plays it, #1
// releases it; #8–#18 and #23 set and read fields of the descriptor that this implementation
// only stores. Music: miscTBD #14 resolves a track name, #40 registers it as a stream, #43
// plays it, #48 sets the repeat mode. Playback itself is the platform's job: this file queues
// requests (`take_sound_requests`, `take_music_requests`) for the frame pump to hand over.
//
// Behaviour per ordinal is the emulator's (reference/eapp-loader/lib.rs: AudioSfxRegister,
// SfxSetBuffer, SfxPlay, AudioRelease, AudioFieldSet/Get, AudioRegister, AudioPlay,
// AudioRepeat, and the audit defaults in install_audit_audio).
#include "framework/audio.h"

#include "host_state.h"
#include "ipod_eapp.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace minigolf::eapp {

namespace {

constexpr size_t SFX_SLOTS = 64;
constexpr uint32_t SFX_TABLE_FULL = ~0u;
constexpr uint32_t FIELD_STATE = 0x3d;

struct AudioState {
    std::vector<std::string> sfx_by_handle;  // path of the .wav each handle plays; "" if unknown
    std::set<uint32_t> looping_handles;
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> fields;  // (handle, offset) -> value
    std::vector<std::string> streams;                          // registered music, by index
    uint32_t music_repeat = 0;
    // The device's output level (#53). Full until the game's Volume page says otherwise, which
    // is what the hardware would have reported at start-up for a player who had not turned it
    // down: `init.cpp` reads it back at once to place the slider.
    uint32_t level = AUDIO_LEVEL_MAX;
    std::vector<SoundRequest> sound_requests;
    std::vector<MusicRequest> music_requests;
};

AudioState& engine() {
    static AudioState instance;
    return instance;
}

// A descriptor field this implementation only stores. `byte` marks the ones the hardware kept
// to eight bits.
void set_field(uint32_t sound, uint32_t offset, uint32_t value, bool byte) {
    engine().fields[{sound, offset}] = byte ? value & 0xff : value;
}

// The file a handle plays: what #7 attached, else the .wav opened in the same order.
std::string sound_for_handle(uint32_t handle) {
    if (handle < engine().sfx_by_handle.size() && !engine().sfx_by_handle[handle].empty()) {
        return engine().sfx_by_handle[handle];
    }
    if (handle < opened_wav_files().size()) {
        return opened_wav_files()[handle];
    }
    return {};
}

}  // namespace

std::vector<SoundRequest> take_sound_requests() {
    std::vector<SoundRequest> requests;
    requests.swap(engine().sound_requests);
    return requests;
}

std::vector<MusicRequest> take_music_requests() {
    std::vector<MusicRequest> requests;
    requests.swap(engine().music_requests);
    return requests;
}

uint32_t audio_level() {
    return engine().level;
}

void set_audio_level(uint32_t level) {
    engine().level = std::min<uint32_t>(level, AUDIO_LEVEL_MAX);
}

}  // namespace minigolf::eapp

namespace minigolf::audio {

using namespace minigolf::eapp;  // NOLINT(google-build-using-namespace): one file, by design

// #0 create(-, sound) -> handle: names the bank file `<course>bank/<sound>.wav` if it exists.
uint32_t create_sound(uint32_t enabled, uint32_t index) {
    log_call("Audio", 0, {enabled, index});
    if (engine().sfx_by_handle.size() >= SFX_SLOTS) {
        return SFX_TABLE_FULL;
    }
    const uint32_t sound = static_cast<uint32_t>(engine().sfx_by_handle.size());
    const std::string relative = current_course() + "bank/" + std::to_string(index) + ".wav";
    std::error_code error;
    const bool exists =
        std::filesystem::exists(std::filesystem::path(game_directory()) / relative, error);
    engine().sfx_by_handle.push_back(exists ? relative : std::string());
    return sound;
}

// #1 release(sound): forget its fields and stop it.
void release_sound(uint32_t sound) {
    log_call("Audio", 1, {sound});
    for (auto it = engine().fields.begin(); it != engine().fields.end();) {
        it = it->first.first == sound ? engine().fields.erase(it) : std::next(it);
    }
    engine().looping_handles.erase(sound);
    const std::string file = sound_for_handle(sound);
    if (!file.empty()) {
        engine().sound_requests.push_back({file, SoundAction::Stop});
    }
}

// #2 play(sound).
void play_sound(uint32_t sound) {
    log_call("Audio", 2, {sound});
    const std::string file = sound_for_handle(sound);
    if (file.empty()) {
        return;
    }
    const bool looping = engine().looping_handles.count(sound) != 0;
    engine().sound_requests.push_back(
        {file, looping ? SoundAction::PlayLooping : SoundAction::Play});
}

// #5 stop(sound): the hardware took it and answered 0. It also stopped the sound, which this
// did not until the option to turn the sound effects off was found not to silence anything
// already sounding.
void stop_sound(uint32_t sound) {
    log_call("Audio", 5, {sound});
    const std::string file = sound_for_handle(sound);
    if (!file.empty()) {
        engine().sound_requests.push_back({file, SoundAction::Stop});
    }
}

// #7 set buffer(sound, pcm): the PCM the game loaded, mapped back to its file by where the file
// layer put it.
void set_sound_buffer(uint32_t sound, GuestAddress pcm) {
    log_call("Audio", 7, {sound, pcm});
    std::string name;
    for (const FileExtent& extent : file_extents()) {
        if (pcm >= extent.begin && pcm < extent.end) {
            name = extent.path;
            break;
        }
    }
    if (sound < engine().sfx_by_handle.size()) {
        engine().sfx_by_handle[sound] = name;
    }
}

// #8-#18: descriptor fields the game sets. Stored only.
void set_sound_data_size(uint32_t sound, uint32_t bytes) {
    log_call("Audio", 8, {sound, bytes});
    set_field(sound, 0x08, bytes, false);
}

void set_sound_reserved(uint32_t sound, uint32_t value) {
    log_call("Audio", 9, {sound, value});
    set_field(sound, 0x0c, value, true);
}

void set_sound_sample_rate(uint32_t sound, uint32_t hertz) {
    log_call("Audio", 10, {sound, hertz});
    set_field(sound, 0x10, hertz, false);
}

void set_sound_channels(uint32_t sound, uint32_t channels) {
    log_call("Audio", 11, {sound, channels});
    set_field(sound, 0x14, channels, false);
}

void set_sound_bits(uint32_t sound, uint32_t bits_per_sample) {
    log_call("Audio", 12, {sound, bits_per_sample});
    set_field(sound, 0x18, bits_per_sample, false);
}

void set_sound_volume(uint32_t sound, uint32_t volume) {
    log_call("Audio", 13, {sound, volume});
    set_field(sound, 0x1c, volume, false);
}

void set_sound_pan(uint32_t sound, uint32_t pan) {
    log_call("Audio", 14, {sound, pan});
    set_field(sound, 0x24, pan, false);
}

void set_sound_rate(uint32_t sound, uint32_t rate) {
    log_call("Audio", 15, {sound, rate});
    set_field(sound, 0x20, rate, false);
}

void set_sound_ready(uint32_t sound, uint32_t ready) {
    log_call("Audio", 17, {sound, ready});
    set_field(sound, FIELD_STATE, ready, true);
}

void set_sound_state(uint32_t sound, uint32_t state) {
    log_call("Audio", 18, {sound, state});
    set_field(sound, 0x3e, state, true);
}

// #23 the descriptor's +4: where the voice put the PCM.
GuestAddress sound_data(uint32_t sound) {
    log_call("Audio", 23, {sound});
    const auto found = engine().fields.find({sound, 0x04});
    return found == engine().fields.end() ? 0 : found->second;
}

// #40 register stream: the track the device most recently resolved; answers its index.
uint32_t register_stream(GuestAddress descriptor, uint32_t reserved, uint32_t flags,
                         GuestAddress stream) {
    log_call("Audio", 40, {descriptor, reserved, flags, stream});
    engine().streams.push_back(take_pending_resource_name());
    return static_cast<uint32_t>(engine().streams.size() - 1);
}

// #43 play stream(index).
void play_stream(uint32_t stream) {
    log_call("Audio", 43, {stream});
    if (stream < engine().streams.size()) {
        engine().music_requests.push_back(
            {engine().streams[stream], engine().music_repeat != 0, MusicAction::Play});
    }
}

// #48 repeat mode(mode): 0 off, 1 one, 2 all.
void set_repeat_mode(uint32_t mode) {
    log_call("Audio", 48, {mode});
    engine().music_repeat = std::min<uint32_t>(mode & 0xff, 2);
}

// #45 stop the music. Turning Music off on the Options screen is this call and nothing else
// (menu.cpp), so a stop that only logged meant the track played on regardless of the setting.
void stop_music() {
    log_call("Audio", 45, {});
    engine().music_requests.push_back({std::string(), false, MusicAction::Stop});
}

// #51/#53 the music level, and #52 the scale it is expressed in (0xff). The Volume page walks
// the level up and down with the wheel and hands it here; the platform reads it back through
// `audio_level` and applies it. Reading it back (#51) is how the game places the slider when
// the page opens, so a level that was always answered as zero drew an empty bar every time.
uint32_t music_level() {
    log_call("Audio", 51, {});
    return engine().level;
}

void set_music_level(uint32_t level) {
    log_call("Audio", 53, {level});
    engine().level = std::min<uint32_t>(level, AUDIO_LEVEL_MAX);
}

uint32_t music_level_scale() {
    log_call("Audio", 52, {});
    return 0xff;
}

// #56, #55, #42: engine queries the hardware answered with a constant.
uint32_t engine_ready() {
    log_call("Audio", 56, {});
    return 0;
}

uint32_t engine_reset() {
    log_call("Audio", 55, {});
    return 0;
}

uint32_t stream_finished() {
    log_call("Audio", 42, {});
    return 0;
}

}  // namespace minigolf::audio
