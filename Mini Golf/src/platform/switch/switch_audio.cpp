// See switch_audio.h.
#include "platform/switch/switch_audio.h"

#include "platform/wav.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <switch.h>
#include <vector>

namespace minigolf::platform {

namespace {

constexpr unsigned DEVICE_RATE = WAV_OUTPUT_RATE, DEVICE_CHANNELS = WAV_OUTPUT_CHANNELS;
constexpr unsigned VOICE_LIMIT = 4;  // the iPod's polyphony, kept

// One buffer is a fifth of a frame's worth of sound at 60 Hz; three of them are in flight, so the
// device always has something to play while the game is busy. 1024 stereo frames of 16-bit sound
// is exactly the 0x1000 bytes audout wants a buffer aligned and sized to.
constexpr unsigned BUFFER_FRAMES = 1024, BUFFER_COUNT = 3;
constexpr unsigned BUFFER_SAMPLES = BUFFER_FRAMES * DEVICE_CHANNELS;

// A sound as the device wants it: 48 kHz, stereo, 16-bit signed (see wav.h).
struct Clip {
    std::vector<int16_t> samples;  // interleaved left, right
};

struct Voice {
    std::string path;
    const Clip* clip = nullptr;
    size_t cursor = 0;  // in samples, not frames
    bool looping = false;
};

struct Mixer {
    bool open = false;
    std::map<std::string, Clip> clips;
    Voice voices[VOICE_LIMIT];
    AudioOutBuffer buffers[BUFFER_COUNT]{};
    bool queued[BUFFER_COUNT] = {};
};

Mixer& mixer() {
    static Mixer instance;
    return instance;
}

// The device reads straight out of this memory, so it lives as long as the program and is aligned
// the way audout insists on.
alignas(0x1000) int16_t buffer_memory[BUFFER_COUNT][BUFFER_SAMPLES];

// A sound is read once and kept in the format the device takes; wav.h does the decoding, which
// is portable and tested on the desktop (tests/unit/wav_test.cpp) because none of the tests can
// be run on the console.
const Clip* clip_for(const std::string& path) {
    Mixer& state = mixer();
    const auto found = state.clips.find(path);
    if (found != state.clips.end()) {
        return found->second.samples.empty() ? nullptr : &found->second;
    }
    Clip& clip = state.clips[path];
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return nullptr;
    }
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    std::vector<uint8_t> bytes(size > 0 ? static_cast<size_t>(size) : 0);
    const size_t read = bytes.empty() ? 0 : std::fread(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
    bytes.resize(read);
    if (!wav_decode(bytes, clip.samples)) {
        clip.samples.clear();
        return nullptr;
    }
    return &clip;
}

// --- mixing -----------------------------------------------------------------------------------

// One buffer's worth of everything playing, summed and clipped.
void fill(int16_t* out) {
    std::memset(out, 0, BUFFER_SAMPLES * sizeof(int16_t));
    for (Voice& voice : mixer().voices) {
        // A clip with no samples in it would loop for ever below, since the cursor can never
        // reach the end of nothing. `clip_for` never hands one over, and this makes sure.
        if (voice.clip == nullptr || voice.clip->samples.empty()) {
            voice.clip = nullptr;
            continue;
        }
        for (unsigned i = 0; i < BUFFER_SAMPLES; ++i) {
            if (voice.cursor >= voice.clip->samples.size()) {
                if (!voice.looping) {
                    voice.clip = nullptr;
                    voice.path.clear();
                    break;
                }
                voice.cursor = 0;
            }
            const int sum = out[i] + voice.clip->samples[voice.cursor++];
            out[i] = static_cast<int16_t>(std::clamp(sum, -32768, 32767));
        }
    }
}

}  // namespace

void audio_open() {
    Mixer& state = mixer();
    if (R_FAILED(audoutInitialize())) {
        return;  // no audio device: the game plays on in silence
    }
    if (R_FAILED(audoutStartAudioOut())) {
        audoutExit();
        return;
    }
    for (unsigned i = 0; i < BUFFER_COUNT; ++i) {
        state.buffers[i].next = nullptr;
        state.buffers[i].buffer = buffer_memory[i];
        state.buffers[i].buffer_size = sizeof buffer_memory[i];
        state.buffers[i].data_size = sizeof buffer_memory[i];
        state.buffers[i].data_offset = 0;
        state.queued[i] = false;
    }
    state.open = true;
    audio_service();  // prime the device with silence so it has something to play
}

void audio_close() {
    Mixer& state = mixer();
    if (!state.open) {
        return;
    }
    audoutStopAudioOut();
    audoutExit();
    state.open = false;
}

void audio_play(const std::string& wav_path, bool looping) {
    Mixer& state = mixer();
    if (!state.open) {
        return;
    }
    const Clip* clip = clip_for(wav_path);
    if (clip == nullptr) {
        return;
    }
    // The voice already playing this sound takes it again — a retrigger restarts it — and
    // otherwise any silent one will do. All four busy means the polyphony is used up.
    Voice* chosen = nullptr;
    for (Voice& voice : state.voices) {
        if (voice.path == wav_path) {
            chosen = &voice;
            break;
        }
        if (chosen == nullptr && voice.clip == nullptr) {
            chosen = &voice;
        }
    }
    if (chosen == nullptr) {
        return;
    }
    chosen->path = wav_path;
    chosen->clip = clip;
    chosen->cursor = 0;
    chosen->looping = looping;
}

void audio_stop(const std::string& wav_path) {
    for (Voice& voice : mixer().voices) {
        if (voice.path == wav_path) {
            voice.clip = nullptr;
            voice.path.clear();
        }
    }
}

void audio_service() {
    Mixer& state = mixer();
    if (!state.open) {
        return;
    }
    // Whatever the device has finished with comes back here to be filled again.
    for (;;) {
        AudioOutBuffer* released = nullptr;
        uint32_t count = 0;
        if (R_FAILED(audoutWaitPlayFinish(&released, &count, 0)) || count == 0) {
            break;
        }
        for (unsigned i = 0; i < BUFFER_COUNT; ++i) {
            if (&state.buffers[i] == released) {
                state.queued[i] = false;
            }
        }
    }
    for (unsigned i = 0; i < BUFFER_COUNT; ++i) {
        if (state.queued[i]) {
            continue;
        }
        fill(buffer_memory[i]);
        if (R_SUCCEEDED(audoutAppendAudioOutBuffer(&state.buffers[i]))) {
            state.queued[i] = true;
        }
    }
}

}  // namespace minigolf::platform
