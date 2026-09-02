// Reading a .wav (src/platform/wav.{h,cpp}).
//
// This is the console platforms' decoder: where SDL hands a file to the system, a console is given
// one PCM stream and has to do everything itself. It is tested here rather than there because
// nothing can be run on the console — the Switch build is cross-compiled and never executed on
// this machine, so the parts of it that are plain logic are kept portable and tested where they
// can be.
#include "platform/wav.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace minigolf::platform;

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void put32(std::vector<uint8_t>& out, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        out.push_back(static_cast<uint8_t>(value >> (i * 8)));
    }
}
void put16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
}
void put_tag(std::vector<uint8_t>& out, const char* tag) {
    out.insert(out.end(), tag, tag + 4);
}

// A .wav around the given sample bytes, as the game's own files are laid out.
std::vector<uint8_t> wav_file(unsigned channels, unsigned rate, unsigned bits,
                              const std::vector<uint8_t>& data, bool extra_chunk = false) {
    std::vector<uint8_t> out;
    put_tag(out, "RIFF");
    put32(out, 0);  // filled in at the end
    put_tag(out, "WAVE");
    if (extra_chunk) {  // a chunk the decoder must step over rather than trip on
        put_tag(out, "LIST");
        put32(out, 5);
        out.insert(out.end(), {'I', 'N', 'F', 'O', 'x'});
        out.push_back(0);  // odd-length chunks are padded
    }
    put_tag(out, "fmt ");
    put32(out, 16);
    put16(out, 1);  // PCM
    put16(out, static_cast<uint16_t>(channels));
    put32(out, rate);
    put32(out, rate * channels * (bits / 8));
    put16(out, static_cast<uint16_t>(channels * (bits / 8)));
    put16(out, static_cast<uint16_t>(bits));
    put_tag(out, "data");
    put32(out, static_cast<uint32_t>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
    const uint32_t size = static_cast<uint32_t>(out.size() - 8);
    std::memcpy(out.data() + 4, &size, 4);  // little-endian host, as guest.h asserts
    return out;
}

void test_already_in_the_right_format() {
    // 16-bit stereo at the output rate: every sample should come back exactly.
    std::vector<uint8_t> data;
    for (int16_t value : {static_cast<int16_t>(0), static_cast<int16_t>(1000),
                          static_cast<int16_t>(-1000), static_cast<int16_t>(32767)}) {
        put16(data, static_cast<uint16_t>(value));
    }
    std::vector<int16_t> samples;
    check(wav_decode(wav_file(2, WAV_OUTPUT_RATE, 16, data), samples), "a plain PCM file decodes");
    check(samples.size() == 4, "two stereo frames come back as four samples");
    check(samples[0] == 0 && samples[1] == 1000 && samples[2] == -1000 && samples[3] == 32767,
          "and every one of them unchanged");
}

void test_mono_and_eight_bit() {
    // 8-bit samples are unsigned around 128; mono plays out of both speakers.
    const std::vector<uint8_t> data = {128, 255, 0};
    std::vector<int16_t> samples;
    check(wav_decode(wav_file(1, WAV_OUTPUT_RATE, 8, data), samples), "8-bit mono decodes");
    check(samples.size() == 6, "three mono frames come back as three stereo pairs");
    check(samples[0] == 0 && samples[1] == 0, "silence is the middle of the 8-bit range");
    check(samples[2] == 32512 && samples[3] == 32512, "the top of it is near full scale");
    check(samples[4] == -32768 && samples[5] == -32768, "and the bottom is the other end");
}

void test_resampling() {
    // Half the output rate: every frame should be heard twice.
    std::vector<uint8_t> data;
    put16(data, static_cast<uint16_t>(100));
    put16(data, static_cast<uint16_t>(200));
    std::vector<int16_t> samples;
    check(wav_decode(wav_file(1, WAV_OUTPUT_RATE / 2, 16, data), samples), "a slower file decodes");
    check(samples.size() == 8, "two frames at half the rate become four");
    check(samples[0] == 100 && samples[2] == 100, "the first sample is held for two frames");
    check(samples[4] == 200 && samples[6] == 200, "and so is the second");
}

void test_chunks_are_walked() {
    std::vector<uint8_t> data;
    put16(data, static_cast<uint16_t>(1234));
    std::vector<int16_t> samples;
    check(wav_decode(wav_file(1, WAV_OUTPUT_RATE, 16, data, true), samples),
          "a file with a chunk this decoder does not know still decodes");
    check(samples.size() == 2 && samples[0] == 1234, "and the samples are found past it");
}

void test_refusals() {
    std::vector<int16_t> samples;
    check(!wav_decode({}, samples), "an empty file is not a .wav");
    check(!wav_decode({'R', 'I', 'F', 'F', 0, 0, 0, 0, 'A', 'V', 'I', ' '}, samples),
          "nor is a RIFF file of some other kind");

    // Compressed, and 24-bit: both are PCM .wav files this cannot turn into samples.
    std::vector<uint8_t> compressed = wav_file(1, WAV_OUTPUT_RATE, 16, {0, 0});
    compressed[20] = 2;  // the format tag, no longer 1
    check(!wav_decode(compressed, samples), "a compressed file is refused");
    check(!wav_decode(wav_file(1, WAV_OUTPUT_RATE, 24, {0, 0, 0}), samples),
          "and so is one with samples of a size this does not read");

    // A file whose data chunk claims more than the file holds must not be read past its end.
    std::vector<uint8_t> truncated = wav_file(1, WAV_OUTPUT_RATE, 16, {1, 2, 3, 4});
    truncated[truncated.size() - 8] = 0xff;  // the data chunk's length, now enormous
    check(!wav_decode(truncated, samples), "a file that lies about its length is refused");
}

}  // namespace

int main() {
    test_already_in_the_right_format();
    test_mono_and_eight_bit();
    test_resampling();
    test_chunks_are_walked();
    test_refusals();
    if (failures != 0) {
        std::fprintf(stderr, "wav: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("wav: ok");
    return 0;
}
