// See music_decoder.h. The macOS and Windows decoders, and the honest nothing everywhere else.
//
// Both are the system's own: ExtAudioFile on macOS, a Media Foundation source reader on
// Windows. Each is asked for 16-bit interleaved samples at the file's own rate, so the whole
// business of AAC — frames per packet, priming, the container — stays inside the system and what
// comes out is what SDL takes. Both are C APIs (COM, on Windows), so this is a plain .cpp; the
// frameworks and libraries are linked in the title's CMakeLists.txt.
#include "ipod/platform/sdl3/music_decoder.h"

#include <cstdio>

#if defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#elif defined(_WIN32)
// clang-format off
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
// clang-format on
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#elif defined(__ANDROID__)
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
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

#elif defined(_WIN32)

namespace {

constexpr unsigned BITS_PER_SAMPLE = 16;
constexpr DWORD AUDIO_STREAM = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);

// A track being read: the reader, and the tail of the last sample the caller had no room for.
// Media Foundation hands back whole samples of its own size — a few thousand frames each — and
// the caller asks for chunks of its own, so the two never line up and the remainder waits here.
struct Track {
    IMFSourceReader* reader = nullptr;
    std::vector<uint8_t> pending;
    size_t pending_taken = 0;
    bool ended = false;
    int bytes_per_frame = 0;
};

Track* as_track(void* handle) {
    return static_cast<Track*>(handle);
}

// Media Foundation is started once and left running: MFShutdown at exit would have to be
// ordered against SDL's own COM use, and there is nothing to gain from it.
bool start_media_foundation() {
    static bool started = false;
    static bool answer = false;
    if (!started) {
        started = true;
        // Either apartment does; SDL has usually initialised this thread's COM already, and a
        // mode that differs from its choice is reported, not fatal.
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        (void)com;
        answer = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
        if (!answer) {
            std::fprintf(stderr, "music: Media Foundation is not available on this Windows\n");
        }
    }
    return answer;
}

// The file's path as Media Foundation takes it: UTF-16, with the separators Windows prefers,
// since the game's own names arrive joined with forward slashes.
std::wstring wide_path(const std::string& path) {
    std::string fixed = path;
    for (char& c : fixed) {
        if (c == '/') {
            c = '\\';
        }
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, fixed.c_str(), -1, nullptr, 0);
    if (needed <= 0) {
        return std::wstring();
    }
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, fixed.c_str(), -1, &out[0], needed);
    out.resize(static_cast<size_t>(needed) - 1);
    return out;
}

// Take the next sample's bytes into `track.pending`. False at the end of the stream, or where a
// damaged track stops decoding — which ends it there rather than taking the program with it.
bool fetch_sample(Track& track) {
    while (true) {
        DWORD flags = 0;
        IMFSample* sample = nullptr;
        const HRESULT read =
            track.reader->ReadSample(AUDIO_STREAM, 0, nullptr, &flags, nullptr, &sample);
        if (FAILED(read) || (flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0 ||
            (flags & MF_SOURCE_READERF_ERROR) != 0) {
            if (sample != nullptr) {
                sample->Release();
            }
            return false;
        }
        if (sample == nullptr) {
            continue;  // a gap or a tick with no data behind it; the next call has some
        }
        IMFMediaBuffer* buffer = nullptr;
        const HRESULT joined = sample->ConvertToContiguousBuffer(&buffer);
        sample->Release();
        if (FAILED(joined) || buffer == nullptr) {
            return false;
        }
        BYTE* bytes = nullptr;
        DWORD length = 0;
        if (FAILED(buffer->Lock(&bytes, nullptr, &length))) {
            buffer->Release();
            return false;
        }
        track.pending.assign(bytes, bytes + length);
        track.pending_taken = 0;
        buffer->Unlock();
        buffer->Release();
        if (length != 0) {
            return true;
        }
    }
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
    if (!start_media_foundation()) {
        return false;
    }
    IMFSourceReader* reader = nullptr;
    const HRESULT opened = MFCreateSourceReaderFromURL(wide_path(path).c_str(), nullptr, &reader);
    if (FAILED(opened) || reader == nullptr) {
        std::fprintf(stderr, "music: cannot open %s (HRESULT 0x%08lx)\n", path.c_str(),
                     static_cast<unsigned long>(opened));
        return false;
    }

    // PCM, 16-bit, and otherwise as the file is: the reader keeps the source's rate and channel
    // count when the type asked for names neither, so both are read back rather than assumed.
    IMFMediaType* wanted = nullptr;
    bool converted = SUCCEEDED(MFCreateMediaType(&wanted)) && wanted != nullptr;
    if (converted) {
        converted = SUCCEEDED(wanted->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) &&
                    SUCCEEDED(wanted->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM)) &&
                    SUCCEEDED(wanted->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, BITS_PER_SAMPLE)) &&
                    SUCCEEDED(reader->SetStreamSelection(
                        static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE)) &&
                    SUCCEEDED(reader->SetStreamSelection(AUDIO_STREAM, TRUE)) &&
                    SUCCEEDED(reader->SetCurrentMediaType(AUDIO_STREAM, nullptr, wanted));
        wanted->Release();
    }
    IMFMediaType* actual = nullptr;
    UINT32 channels = 0, rate = 0;
    if (converted) {
        converted = SUCCEEDED(reader->GetCurrentMediaType(AUDIO_STREAM, &actual)) &&
                    actual != nullptr &&
                    SUCCEEDED(actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels)) &&
                    SUCCEEDED(actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate)) &&
                    channels != 0 && rate != 0;
        if (actual != nullptr) {
            actual->Release();
        }
    }
    if (!converted) {
        std::fprintf(stderr, "music: cannot decode %s to PCM\n", path.c_str());
        reader->Release();
        return false;
    }

    Track* track = new Track;
    track->reader = reader;
    track->bytes_per_frame = static_cast<int>(channels * BITS_PER_SAMPLE / 8);
    handle_ = track;
    channels_ = static_cast<int>(channels);
    spec.format = SDL_AUDIO_S16LE;
    spec.channels = channels_;
    spec.freq = static_cast<int>(rate);
    return true;
}

int MusicDecoder::read(void* into, int frames) {
    if (handle_ == nullptr || frames <= 0) {
        return 0;
    }
    Track& track = *as_track(handle_);
    uint8_t* out = static_cast<uint8_t*>(into);
    const size_t wanted = static_cast<size_t>(frames) * static_cast<size_t>(track.bytes_per_frame);
    size_t written = 0;
    while (written < wanted) {
        if (track.pending_taken >= track.pending.size()) {
            if (track.ended || !fetch_sample(track)) {
                track.ended = true;
                break;
            }
        }
        const size_t available = track.pending.size() - track.pending_taken;
        const size_t take = available < wanted - written ? available : wanted - written;
        std::memcpy(out + written, track.pending.data() + track.pending_taken, take);
        track.pending_taken += take;
        written += take;
    }
    return static_cast<int>(written / static_cast<size_t>(track.bytes_per_frame));
}

void MusicDecoder::restart() {
    if (handle_ == nullptr) {
        return;
    }
    Track& track = *as_track(handle_);
    PROPVARIANT start;
    PropVariantInit(&start);
    start.vt = VT_I8;
    start.hVal.QuadPart = 0;
    (void)track.reader->SetCurrentPosition(GUID_NULL, start);
    PropVariantClear(&start);
    track.pending.clear();
    track.pending_taken = 0;
    track.ended = false;
}

void MusicDecoder::close() {
    if (handle_ != nullptr) {
        Track* track = as_track(handle_);
        if (track->reader != nullptr) {
            track->reader->Release();
        }
        delete track;
        handle_ = nullptr;
    }
    channels_ = 0;
}

#elif defined(__ANDROID__)

namespace {

constexpr unsigned BITS_PER_SAMPLE = 16;
// ENCODING_PCM_16BIT, as android.media.AudioFormat numbers it. The key is absent on almost every
// device, and 16-bit is the default when it is, but a decoder that announced something else would
// otherwise be copied out as noise.
constexpr int32_t PCM_16BIT = 2;
// How long a dequeue waits. `service()` runs on the frame thread, so this is short and the pump
// goes round again rather than holding a frame; the half-second the player keeps queued ahead is
// what actually absorbs the codec's latency.
constexpr int64_t DEQUEUE_TIMEOUT_US = 4000;
// A codec that answers "not yet" for this many turns running is not going to answer at all.
constexpr int IDLE_LIMIT = 64;

// A track being decoded: the two objects that do it, the file they read, and the tail of the
// last buffer the caller had no room for. MediaCodec hands back whole access units of its own
// size and the caller asks for chunks of its own, so the two never line up — the same reason
// the Windows reader above keeps one.
struct Track {
    AMediaExtractor* extractor = nullptr;
    AMediaCodec* codec = nullptr;
    int fd = -1;
    std::vector<uint8_t> pending;
    size_t pending_taken = 0;
    bool ended = false;       // the codec has said end-of-stream
    bool input_done = false;  // the extractor is drained and the codec told so
    int bytes_per_frame = 0;
};

Track* as_track(void* handle) {
    return static_cast<Track*>(handle);
}

void destroy(Track* track) {
    if (track == nullptr) {
        return;
    }
    if (track->codec != nullptr) {
        AMediaCodec_stop(track->codec);
        AMediaCodec_delete(track->codec);
    }
    if (track->extractor != nullptr) {
        AMediaExtractor_delete(track->extractor);
    }
    if (track->fd >= 0) {
        ::close(track->fd);
    }
    delete track;
}

// Hand the codec one access unit, or tell it there are no more. Doing nothing is fine: it means
// the codec has no free input buffer this turn, and the drain below is what makes room.
void feed_input(Track& track) {
    if (track.input_done) {
        return;
    }
    const ssize_t index = AMediaCodec_dequeueInputBuffer(track.codec, 0);
    if (index < 0) {
        return;
    }
    const size_t slot = static_cast<size_t>(index);
    size_t capacity = 0;
    uint8_t* buffer = AMediaCodec_getInputBuffer(track.codec, slot, &capacity);
    const ssize_t got = buffer == nullptr
                            ? -1
                            : AMediaExtractor_readSampleData(track.extractor, buffer, capacity);
    if (got <= 0) {
        (void)AMediaCodec_queueInputBuffer(track.codec, slot, 0, 0, 0,
                                           AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
        track.input_done = true;
        return;
    }
    const int64_t when = AMediaExtractor_getSampleTime(track.extractor);
    (void)AMediaCodec_queueInputBuffer(track.codec, slot, 0, static_cast<size_t>(got),
                                       static_cast<uint64_t>(when < 0 ? 0 : when), 0);
    (void)AMediaExtractor_advance(track.extractor);
}

// Run the codec until it gives up one buffer of PCM, which lands in `track.pending`. False at the
// end of the stream, and on a codec that stops answering — which ends the track there rather
// than spinning on the frame thread for ever.
//
// `format` is filled in whenever the codec announces one. A decoder always announces its output
// format before its first buffer, and that announcement — not the extractor's idea of the track —
// is the only trustworthy source of the sample rate and channel count.
bool fetch_sample(Track& track, AMediaFormat** format = nullptr) {
    int idle = 0;
    while (idle < IDLE_LIMIT) {
        feed_input(track);

        AMediaCodecBufferInfo info{};
        const ssize_t index = AMediaCodec_dequeueOutputBuffer(track.codec, &info,
                                                              DEQUEUE_TIMEOUT_US);
        if (index >= 0) {
            idle = 0;
            const size_t slot = static_cast<size_t>(index);
            size_t capacity = 0;
            const uint8_t* buffer = AMediaCodec_getOutputBuffer(track.codec, slot, &capacity);
            const bool have = buffer != nullptr && info.size > 0;
            if (have) {
                const uint8_t* start = buffer + info.offset;
                track.pending.assign(start, start + static_cast<size_t>(info.size));
                track.pending_taken = 0;
            }
            const bool finished = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
            (void)AMediaCodec_releaseOutputBuffer(track.codec, slot, false);
            if (have) {
                return true;
            }
            if (finished) {
                track.ended = true;
                return false;
            }
            continue;
        }
        if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            idle = 0;
            if (format != nullptr) {
                *format = AMediaCodec_getOutputFormat(track.codec);
            }
            continue;
        }
        if (index == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
            idle = 0;
            continue;
        }
        if (index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
            ++idle;  // the codec is still working, or is finished and has nothing left to say
            continue;
        }
        break;  // anything else is an error
    }
    track.ended = true;
    return false;
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
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        SDL_Log("music: cannot open %s", path.c_str());
        return false;
    }
    struct stat info {};
    if (fstat(fd, &info) != 0 || info.st_size <= 0) {
        ::close(fd);
        return false;
    }

    Track* track = new Track;
    track->fd = fd;
    track->extractor = AMediaExtractor_new();
    if (track->extractor == nullptr ||
        AMediaExtractor_setDataSourceFd(track->extractor, fd, 0,
                                        static_cast<off64_t>(info.st_size)) != AMEDIA_OK) {
        SDL_Log("music: %s is not something MediaExtractor can read", path.c_str());
        destroy(track);
        return false;
    }

    // The first audio track in the file. These are single-track .m4a files, but the loop is the
    // honest way to ask and costs nothing.
    const size_t tracks = AMediaExtractor_getTrackCount(track->extractor);
    AMediaFormat* chosen = nullptr;
    const char* mime = nullptr;
    for (size_t i = 0; i < tracks; ++i) {
        AMediaFormat* candidate = AMediaExtractor_getTrackFormat(track->extractor, i);
        const char* type = nullptr;
        if (candidate != nullptr && AMediaFormat_getString(candidate, AMEDIAFORMAT_KEY_MIME,
                                                          &type) &&
            type != nullptr && std::strncmp(type, "audio/", 6) == 0) {
            (void)AMediaExtractor_selectTrack(track->extractor, i);
            chosen = candidate;
            mime = type;
            break;
        }
        if (candidate != nullptr) {
            AMediaFormat_delete(candidate);
        }
    }
    if (chosen == nullptr || mime == nullptr) {
        SDL_Log("music: no audio track in %s", path.c_str());
        destroy(track);
        return false;
    }

    // Configure with the extractor's own format: it carries the codec-specific data an AAC
    // decoder needs, which nothing here would otherwise know how to build.
    track->codec = AMediaCodec_createDecoderByType(mime);
    if (track->codec == nullptr ||
        AMediaCodec_configure(track->codec, chosen, nullptr, nullptr, 0) != AMEDIA_OK ||
        AMediaCodec_start(track->codec) != AMEDIA_OK) {
        SDL_Log("music: no decoder for %s on this device", mime);
        AMediaFormat_delete(chosen);
        destroy(track);
        return false;
    }
    AMediaFormat_delete(chosen);

    // Decode the first buffer here rather than on the first `read`, because that is what makes
    // the codec announce its output format — and the rate and channel count it announces are the
    // ones the audio device has to be opened with. The buffer itself is kept, not thrown away.
    AMediaFormat* produced = nullptr;
    const bool primed = fetch_sample(*track, &produced);
    if (produced == nullptr) {
        produced = AMediaCodec_getOutputFormat(track->codec);
    }
    int32_t rate = 0, channels = 0, encoding = PCM_16BIT;
    const bool described = produced != nullptr &&
                           AMediaFormat_getInt32(produced, AMEDIAFORMAT_KEY_SAMPLE_RATE, &rate) &&
                           AMediaFormat_getInt32(produced, AMEDIAFORMAT_KEY_CHANNEL_COUNT,
                                                 &channels);
    if (produced != nullptr) {
        // Absent on nearly every device, and 16-bit when absent.
        (void)AMediaFormat_getInt32(produced, "pcm-encoding", &encoding);
        AMediaFormat_delete(produced);
    }
    if (!primed || !described || rate <= 0 || channels <= 0) {
        SDL_Log("music: %s decoded to nothing", path.c_str());
        destroy(track);
        return false;
    }
    if (encoding != PCM_16BIT) {
        SDL_Log("music: this device's decoder answers PCM encoding %d, not 16-bit", encoding);
        destroy(track);
        return false;
    }

    track->bytes_per_frame = channels * static_cast<int>(BITS_PER_SAMPLE) / 8;
    handle_ = track;
    channels_ = channels;
    spec.format = SDL_AUDIO_S16LE;
    spec.channels = channels;
    spec.freq = rate;
    return true;
}

int MusicDecoder::read(void* into, int frames) {
    if (handle_ == nullptr || frames <= 0) {
        return 0;
    }
    Track& track = *as_track(handle_);
    uint8_t* out = static_cast<uint8_t*>(into);
    const size_t wanted = static_cast<size_t>(frames) * static_cast<size_t>(track.bytes_per_frame);
    size_t written = 0;
    while (written < wanted) {
        if (track.pending_taken >= track.pending.size()) {
            if (track.ended || !fetch_sample(track)) {
                break;
            }
        }
        const size_t available = track.pending.size() - track.pending_taken;
        const size_t take = available < wanted - written ? available : wanted - written;
        std::memcpy(out + written, track.pending.data() + track.pending_taken, take);
        track.pending_taken += take;
        written += take;
    }
    return static_cast<int>(written / static_cast<size_t>(track.bytes_per_frame));
}

void MusicDecoder::restart() {
    if (handle_ == nullptr) {
        return;
    }
    Track& track = *as_track(handle_);
    (void)AMediaExtractor_seekTo(track.extractor, 0, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
    (void)AMediaCodec_flush(track.codec);
    track.pending.clear();
    track.pending_taken = 0;
    track.ended = false;
    track.input_done = false;
}

void MusicDecoder::close() {
    if (handle_ != nullptr) {
        destroy(as_track(handle_));
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
