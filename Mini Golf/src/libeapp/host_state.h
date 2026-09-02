// State shared between libeapp's framework implementations (not part of the public API).
//
// The public header, include/ipod_eapp.h, declares what the runtime's frame pump needs. The
// declarations here are for the frameworks to talk to each other: the file layer records where
// it loaded each file so the audio layer can tell which sound a PCM pointer belongs to, and the
// resource resolver hands the name it resolved to the next audio-stream registration.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace minigolf::eapp {

// --- input (host_state.cpp) ----------------------------------------------------------------

// The next queued wheel sample as the event word the game expects, or 0 when none is queued.
[[nodiscard]] uint32_t take_input_event();

// --- file completions (host_state.cpp) ------------------------------------------------------

// Owe the game one completion callback for the operation on `request`.
void queue_completion(uint32_t request);

// --- files (async_file.cpp) ----------------------------------------------------------------

// A guest memory range a file's bytes were read into, most recent first. The audio layer uses
// it to map a sound buffer back to the .wav it came from.
struct FileExtent {
    uint32_t begin;
    uint32_t end;
    std::string path;
};
[[nodiscard]] const std::vector<FileExtent>& file_extents();

// Every .wav opened so far, in order of first open. Sound handles without a bank file fall back
// to these by index (the emulator's behaviour, kept so the two agree).
[[nodiscard]] const std::vector<std::string>& opened_wav_files();

// The course whose assets are loaded ("c00", "c01", ...), from the names of the files opened.
[[nodiscard]] const std::string& current_course();

// The directory the game's resources live in, as given on the command line.
[[nodiscard]] const std::string& game_directory();

// --- resource names (misc.cpp) --------------------------------------------------------------

// The last name miscTBD #14 resolved; Audio #40 consumes it when registering a stream.
[[nodiscard]] std::string take_pending_resource_name();

// --- strings in guest memory ----------------------------------------------------------------

// Read a NUL-terminated string from guest memory, at most `max_length` bytes.
[[nodiscard]] std::string read_guest_string(uint32_t address, uint32_t max_length);

}  // namespace minigolf::eapp
