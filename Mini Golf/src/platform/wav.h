// Reading a .wav, for platforms whose sound API takes samples rather than files.
//
// The game's effects are small, plain PCM .wav files. A platform that can hand a file to the
// system (SDL does) has no use for this; one that is given a single PCM stream and left to do its
// own mixing (the Switch, and consoles generally) needs the samples in the format that stream
// wants, which is what this produces.
#pragma once

#include <cstdint>
#include <vector>

namespace minigolf::platform {

// The one format worth converting to: what every console's audio output takes.
constexpr unsigned WAV_OUTPUT_RATE = 48000, WAV_OUTPUT_CHANNELS = 2;

// Decode `file` into interleaved 48 kHz stereo 16-bit samples. Answers false for anything that is
// not plain PCM of one or two channels at 8 or 16 bits — compressed or floating-point .wav files
// are refused rather than played as noise.
[[nodiscard]] bool wav_decode(const std::vector<uint8_t>& file, std::vector<int16_t>& samples);

}  // namespace minigolf::platform
