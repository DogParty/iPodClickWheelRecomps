// See wav.h.
#include "platform/wav.h"

#include <cstring>

namespace minigolf::platform {

namespace {

uint32_t read_le32(const uint8_t* at) {
    return static_cast<uint32_t>(at[0]) | static_cast<uint32_t>(at[1]) << 8 |
           static_cast<uint32_t>(at[2]) << 16 | static_cast<uint32_t>(at[3]) << 24;
}

uint16_t read_le16(const uint8_t* at) {
    return static_cast<uint16_t>(static_cast<uint16_t>(at[0]) | static_cast<uint16_t>(at[1]) << 8);
}

}  // namespace

bool wav_decode(const std::vector<uint8_t>& file, std::vector<int16_t>& samples) {
    samples.clear();
    if (file.size() < 12 || std::memcmp(file.data(), "RIFF", 4) != 0 ||
        std::memcmp(file.data() + 8, "WAVE", 4) != 0) {
        return false;
    }

    unsigned channels = 0, rate = 0, bits = 0;
    const uint8_t* data = nullptr;
    size_t data_size = 0;
    for (size_t at = 12; at + 8 <= file.size();) {
        const uint32_t size = read_le32(file.data() + at + 4);
        const uint8_t* body = file.data() + at + 8;
        if (size > file.size() - at - 8) {
            break;  // a chunk that claims more than the file holds
        }
        if (std::memcmp(file.data() + at, "fmt ", 4) == 0 && size >= 16) {
            if (read_le16(body) != 1) {  // 1 = uncompressed PCM
                return false;
            }
            channels = read_le16(body + 2);
            rate = read_le32(body + 4);
            bits = read_le16(body + 14);
        } else if (std::memcmp(file.data() + at, "data", 4) == 0) {
            data = body;
            data_size = size;
        }
        at += 8 + size + (size & 1);  // chunks are padded to an even length
    }
    if (data == nullptr || channels == 0 || channels > 2 || rate == 0 ||
        (bits != 8 && bits != 16)) {
        return false;
    }

    const unsigned bytes_per_frame = channels * (bits / 8);
    const size_t frames = data_size / bytes_per_frame;
    // Nearest-neighbour resampling. These are short effects on a console's speakers; what an
    // interpolating resampler would add is not something anybody will hear over the golf ball,
    // and this cannot ring or overshoot.
    const size_t out_frames = frames * WAV_OUTPUT_RATE / rate;
    samples.resize(out_frames * WAV_OUTPUT_CHANNELS);
    for (size_t frame = 0; frame < out_frames; ++frame) {
        const size_t source = frame * rate / WAV_OUTPUT_RATE;
        for (unsigned channel = 0; channel < WAV_OUTPUT_CHANNELS; ++channel) {
            const unsigned take = channels == 1 ? 0 : channel;  // mono plays out of both
            const uint8_t* sample = data + source * bytes_per_frame + take * (bits / 8);
            samples[frame * WAV_OUTPUT_CHANNELS + channel] =
                bits == 16 ? static_cast<int16_t>(read_le16(sample))
                           : static_cast<int16_t>((static_cast<int>(*sample) - 128) * 256);
        }
    }
    return true;
}

}  // namespace minigolf::platform
