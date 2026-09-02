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
#include "runtime/memory.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace bowling::eapp {

namespace {

constexpr size_t SFX_SLOTS = 64;
constexpr uint32_t SFX_TABLE_FULL = ~0u;
constexpr uint32_t FIELD_STATE = 0x3d;

// What the game has told us about one of its sound-effect handles. Every field arrives through
// its own framework call before the sound is first played (see SoundRequest in ipod_eapp.h).
struct Voice {
    GuestAddress pcm = 0;
    uint32_t bytes = 0;
    uint32_t sample_rate = 0;
    uint32_t channels = 0;
    uint32_t bits = 0;

    // Enough to play: a buffer, a length, and a format that means something.
    [[nodiscard]] bool playable() const {
        return pcm != 0 && bytes != 0 && sample_rate != 0 && channels != 0 &&
               (bits == 8 || bits == 16);
    }
};

struct AudioState {
    std::vector<Voice> voices;  // by handle, in creation order
    std::set<uint32_t> looping_handles;
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> fields;  // (handle, offset) -> value
    std::vector<std::string> streams;                          // registered music, by index
    uint32_t music_repeat = 0;
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

Voice* voice_for(uint32_t handle) {
    return handle < engine().voices.size() ? &engine().voices[handle] : nullptr;
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

}  // namespace bowling::eapp

namespace bowling::audio {

using namespace bowling::eapp;  // NOLINT(google-build-using-namespace): one file, by design

// #0 create(-, index) -> handle: reserve a voice. What it will play is described afterwards,
// call by call, so nothing is known about it yet.
uint32_t create_sound(uint32_t enabled, uint32_t index) {
    log_call("Audio", 0, {enabled, index});
    if (engine().voices.size() >= SFX_SLOTS) {
        return SFX_TABLE_FULL;
    }
    engine().voices.emplace_back();
    return static_cast<uint32_t>(engine().voices.size() - 1);
}

// #1 release(sound): forget its fields and stop it.
void release_sound(uint32_t sound) {
    log_call("Audio", 1, {sound});
    for (auto it = engine().fields.begin(); it != engine().fields.end();) {
        it = it->first.first == sound ? engine().fields.erase(it) : std::next(it);
    }
    engine().looping_handles.erase(sound);
    engine().sound_requests.push_back({sound, SoundAction::Stop, {}, 0, 0, 0});
}

// #2 play(sound): hand the platform the samples the game described, and the format to read them
// in. Nothing is played until every part of that description has arrived.
void play_sound(uint32_t sound) {
    log_call("Audio", 2, {sound});
    const Voice* voice = voice_for(sound);
    if (voice == nullptr || !voice->playable()) {
        return;
    }
    const uint8_t* from = guest_pointer(voice->pcm, voice->bytes);
    const bool looping = engine().looping_handles.count(sound) != 0;
    engine().sound_requests.push_back({sound,
                                       looping ? SoundAction::PlayLooping : SoundAction::Play,
                                       std::vector<uint8_t>(from, from + voice->bytes),
                                       voice->sample_rate, voice->channels, voice->bits});
}

// #5 stop(sound): the hardware took it and answered 0.
void stop_sound(uint32_t sound) {
    log_call("Audio", 5, {sound});
}

// #7 set buffer(sound, pcm): the PCM the game loaded, mapped back to its file by where the file
// layer put it.
void set_sound_buffer(uint32_t sound, GuestAddress pcm) {
    log_call("Audio", 7, {sound, pcm});
    if (Voice* voice = voice_for(sound); voice != nullptr) {
        voice->pcm = pcm;
    }
}

// #8-#18: descriptor fields the game sets. Stored only.
void set_sound_data_size(uint32_t sound, uint32_t bytes) {
    log_call("Audio", 8, {sound, bytes});
    set_field(sound, 0x08, bytes, false);
    if (Voice* voice = voice_for(sound); voice != nullptr) {
        voice->bytes = bytes;
    }
}

void set_sound_reserved(uint32_t sound, uint32_t value) {
    log_call("Audio", 9, {sound, value});
    set_field(sound, 0x0c, value, true);
}

void set_sound_sample_rate(uint32_t sound, uint32_t hertz) {
    log_call("Audio", 10, {sound, hertz});
    set_field(sound, 0x10, hertz, false);
    if (Voice* voice = voice_for(sound); voice != nullptr) {
        voice->sample_rate = hertz;
    }
}

void set_sound_channels(uint32_t sound, uint32_t channels) {
    log_call("Audio", 11, {sound, channels});
    set_field(sound, 0x14, channels, false);
    if (Voice* voice = voice_for(sound); voice != nullptr) {
        voice->channels = channels;
    }
}

void set_sound_bits(uint32_t sound, uint32_t bits_per_sample) {
    log_call("Audio", 12, {sound, bits_per_sample});
    set_field(sound, 0x18, bits_per_sample, false);
    if (Voice* voice = voice_for(sound); voice != nullptr) {
        voice->bits = bits_per_sample;
    }
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

// #16 how many times a play repeats: zero is "forever", which is the one value that changes
// what the platform is asked to do (reference/eapp-loader/lib.rs, `Stub::SfxRepeat`). Any
// other count plays the sound once here — a repeat count of two or three has not been seen,
// and the emulator does not play it more than once either.
void set_sound_repeat(uint32_t sound, uint32_t count) {
    log_call("Audio", 16, {sound, count});
    if (count == 0) {
        engine().looping_handles.insert(sound);
    } else {
        engine().looping_handles.erase(sound);
    }
}

// #23 the descriptor's +4: where the voice put the PCM.
GuestAddress sound_data(uint32_t sound) {
    log_call("Audio", 23, {sound});
    const auto found = engine().fields.find({sound, 0x04});
    return found == engine().fields.end() ? 0 : found->second;
}

uint32_t registered_stream_count() {
    return static_cast<uint32_t>(engine().streams.size());
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
        engine().music_requests.push_back({engine().streams[stream], engine().music_repeat != 0});
    }
}

// #48 repeat mode(mode): 0 off, 1 one, 2 all.
void set_repeat_mode(uint32_t mode) {
    log_call("Audio", 48, {mode});
    engine().music_repeat = std::min<uint32_t>(mode & 0xff, 2);
}

// #45 stop the music.
void stop_music() {
    log_call("Audio", 45, {});
}

// #51/#53 the music level, and #52 the scale it is expressed in (0xff).
uint32_t music_level() {
    log_call("Audio", 51, {});
    return 0;
}

void set_music_level(uint32_t level) {
    log_call("Audio", 53, {level});
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

}  // namespace bowling::audio
