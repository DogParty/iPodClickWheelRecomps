// See music_decoder.h. The macOS decoder, and the honest nothing everywhere else.
//
// ExtAudioFile is asked for 16-bit interleaved samples at the file's own rate, so the whole
// business of AAC — frames per packet, priming, the container — stays inside AudioToolbox and
// what comes out is what SDL takes. It is a C API, so this is a plain .cpp; the framework is
// linked in CMakeLists.txt beside Cocoa.
#include "ipod/platform/sdl3/music_decoder.h"

#include <cstdio>

#if defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace ipod::platform {

#if defined(__APPLE__)

namespace {

constexpr unsigned BITS_PER_SAMPLE = 16;

ExtAudioFileRef as_file(void* handle) {
    return static_cast<ExtAudioFileRef>(handle);
}

// The format to ask ExtAudioFile for: signed 16-bit, interleaved, at the file's own sample rate
// so nothing is resampled twice (SDL will match the device itself).
AudioStreamBasicDescription client_format(double sample_rate, UInt32 channels) {
    AudioStreamBasicDescription format{};
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    format.mSampleRate = sample_rate;
    format.mChannelsPerFrame = channels;
    format.mBitsPerChannel = BITS_PER_SAMPLE;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = channels * BITS_PER_SAMPLE / 8;
    format.mBytesPerPacket = format.mBytesPerFrame;
    return format;
}

}  // namespace

bool music_decoding_supported() {
    return true;
}

MusicDecoder::~MusicDecoder() {
    close();
}

bool MusicDecoder::open(const std::string& path, SDL_AudioSpec& spec) {
    close();
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        nullptr, reinterpret_cast<const UInt8*>(path.c_str()), static_cast<CFIndex>(path.size()),
        false);
    if (url == nullptr) {
        return false;
    }
    ExtAudioFileRef file = nullptr;
    const OSStatus opened = ExtAudioFileOpenURL(url, &file);
    CFRelease(url);
    if (opened != noErr || file == nullptr) {
        std::fprintf(stderr, "music: cannot open %s (OSStatus %d)\n", path.c_str(),
                     static_cast<int>(opened));
        return false;
    }

    // What the file holds, so the decoded form can keep its rate and channel count.
    AudioStreamBasicDescription source{};
    UInt32 size = sizeof source;
    if (ExtAudioFileGetProperty(file, kExtAudioFileProperty_FileDataFormat, &size, &source) !=
        noErr) {
        ExtAudioFileDispose(file);
        return false;
    }
    const UInt32 channels = source.mChannelsPerFrame != 0 ? source.mChannelsPerFrame : 2;
    const AudioStreamBasicDescription wanted = client_format(source.mSampleRate, channels);
    if (ExtAudioFileSetProperty(file, kExtAudioFileProperty_ClientDataFormat, sizeof wanted,
                                &wanted) != noErr) {
        std::fprintf(stderr, "music: cannot decode %s to PCM\n", path.c_str());
        ExtAudioFileDispose(file);
        return false;
    }

    handle_ = file;
    channels_ = static_cast<int>(channels);
    spec.format = SDL_AUDIO_S16LE;
    spec.channels = channels_;
    spec.freq = static_cast<int>(source.mSampleRate);
    return true;
}

int MusicDecoder::read(void* into, int frames) {
    if (handle_ == nullptr || frames <= 0) {
        return 0;
    }
    // One buffer, interleaved, which is what the client format above asks for.
    AudioBufferList buffers{};
    buffers.mNumberBuffers = 1;
    buffers.mBuffers[0].mNumberChannels = static_cast<UInt32>(channels_);
    buffers.mBuffers[0].mDataByteSize =
        static_cast<UInt32>(frames * channels_ * static_cast<int>(BITS_PER_SAMPLE) / 8);
    buffers.mBuffers[0].mData = into;

    UInt32 produced = static_cast<UInt32>(frames);
    if (ExtAudioFileRead(as_file(handle_), &produced, &buffers) != noErr) {
        return 0;  // a damaged track ends where it broke rather than taking the program with it
    }
    return static_cast<int>(produced);  // 0 is the end of the file
}

void MusicDecoder::restart() {
    if (handle_ != nullptr) {
        (void)ExtAudioFileSeek(as_file(handle_), 0);
    }
}

void MusicDecoder::close() {
    if (handle_ != nullptr) {
        ExtAudioFileDispose(as_file(handle_));
        handle_ = nullptr;
    }
    channels_ = 0;
}

#else

// No decoder on this platform yet. The interface is the whole of what one has to provide.
bool music_decoding_supported() {
    return false;
}

MusicDecoder::~MusicDecoder() = default;

bool MusicDecoder::open(const std::string& /*path*/, SDL_AudioSpec& /*spec*/) {
    return false;
}

int MusicDecoder::read(void* /*into*/, int /*frames*/) {
    return 0;
}

void MusicDecoder::restart() {}

void MusicDecoder::close() {}

#endif

}  // namespace ipod::platform
