// The Switch's sound effects: a small mixer over libnx's `audout`.
//
// The console gives homebrew one PCM stream at 48 kHz, stereo, 16-bit signed, fed a buffer at a
// time — there is no "play this file" call and no mixer. So the game's .wav files are read once,
// converted to that format, and mixed here: four voices, matching the polyphony the iPod had.
//
// Kept apart from switch_platform.cpp because it is the one part of this platform with a shape of
// its own; the platform simply forwards `Platform::play_sound` and friends to these.
#pragma once

#include <string>

namespace minigolf::platform {

// Open the audio device, if there is one. Silent afterwards if not: no sound is a poor console
// but a working game.
void audio_open();
void audio_close();

// Start `wav_path`, restarting it if that sound is already playing. A looping sound repeats until
// it is stopped.
void audio_play(const std::string& wav_path, bool looping);
void audio_stop(const std::string& wav_path);

// Called once a frame: hand the device whatever it has finished with, filled with the next slice
// of everything playing.
void audio_service();

}  // namespace minigolf::platform
