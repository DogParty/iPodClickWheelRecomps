// State shared between libeapp's framework implementations (not part of the public API).
//
// The public header, include/ipod_eapp.h, declares what the runtime's frame pump needs. The
// declarations here are for the frameworks to talk to each other: the resource resolver hands
// the name it resolved to the next audio-stream registration, and the file layer says where the
// game's resources live.
#pragma once

#include <cstdint>
#include <string>

namespace lost::eapp {

// --- input (host_state.cpp) ----------------------------------------------------------------

// The next queued wheel sample as the event word the game expects, or 0 when none is queued.
[[nodiscard]] uint32_t take_input_event();

// --- file completions (host_state.cpp) ------------------------------------------------------

// Owe the game one completion callback for the operation on `request`.
void queue_completion(uint32_t request);

// --- files (async_file.cpp) ----------------------------------------------------------------

// The directory the game's resources live in, as given on the command line.
[[nodiscard]] const std::string& game_directory();

// --- resource names (misc.cpp) --------------------------------------------------------------

// The last name miscTBD #14 resolved; Audio #40 consumes it when registering a stream.
[[nodiscard]] std::string take_pending_resource_name();

// --- strings in guest memory ----------------------------------------------------------------

// Read a NUL-terminated string from guest memory, at most `max_length` bytes.
[[nodiscard]] std::string read_guest_string(uint32_t address, uint32_t max_length);

}  // namespace lost::eapp
