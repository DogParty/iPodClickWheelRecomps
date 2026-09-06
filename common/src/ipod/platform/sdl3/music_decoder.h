// Turning the game's music into PCM an audio device can take.
//
// The six tracks are AAC in an MP4 container (`m0.m4a` … `a2.m4a`, 44.1 kHz stereo, 45 to 115
// seconds each). SDL decodes WAV and nothing else, so the sound effects can be handed straight
// to it and the music cannot. Until now the music was played by spawning `afplay`, which worked
// and nothing more: a child process has no volume this program can set, no way to be mixed with
// the sound effects, and exists only on macOS.
//
// This is the seam that replaces it. A decoder answers PCM in chunks and the caller feeds an
// SDL audio stream with it, exactly as a sound effect's samples are fed — so the music is on
// the same device, at the same volume, stopped by the same call.
//
// A track is decoded as it plays rather than in one go: the longest is 115 seconds, which is
// about 20 MB decoded, and reading it up front would cost that much memory and a visible pause
// at the start of every course.
//
// Each platform decodes with its own system's codec rather than a new dependency: AudioToolbox's
// ExtAudioFile on macOS, a Media Foundation source reader on Windows, MediaExtractor feeding
// MediaCodec on Android. A Windows "N" edition without the Media Feature Pack has no decoder and
// reports that once. Everywhere else — Linux, the Switch — there is none yet and
// `music_decoding_supported()` answers false; the caller says so once and the game plays on in
// silence. Adding a platform means implementing this interface for it and nothing above this
// file changes.
#pragma once

#include <SDL3/SDL.h>

#include <string>

namespace ipod::platform {

// Whether this build can decode the game's music at all.
[[nodiscard]] bool music_decoding_supported();

class MusicDecoder {
public:
    MusicDecoder() = default;
    ~MusicDecoder();
    MusicDecoder(const MusicDecoder&) = delete;
    MusicDecoder& operator=(const MusicDecoder&) = delete;

    // Open `path` and describe the PCM it will produce. False when the file cannot be opened or
    // this build has no decoder; the caller reports it and carries on without music.
    [[nodiscard]] bool open(const std::string& path, SDL_AudioSpec& spec);

    [[nodiscard]] bool is_open() const { return handle_ != nullptr; }

    // Decode up to `frames` frames into `into`, which must hold `frames` × channels samples of
    // the format `open` reported. Answers the frames produced: 0 means the end of the track.
    [[nodiscard]] int read(void* into, int frames);

    // Back to the beginning, for a track that repeats.
    void restart();

    void close();

private:
    void* handle_ = nullptr;  // the decoder's own, whatever this platform's is
    // Only a build that has a decoder reads this back. On one that has none the whole class is
    // the honest nothing described above, and clang — which GCC does not follow here — counts an
    // untouched private field as a warning, which is an error in this project.
    [[maybe_unused]] int channels_ = 0;
};

}  // namespace ipod::platform
