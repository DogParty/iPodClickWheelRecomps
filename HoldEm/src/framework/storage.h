// Reading the game's files, and the small store the settings live in.
//
// Reads are asynchronous: the game fills in a request record (the file, the buffer, how many
// bytes, what to do when it finishes) and hands it over; the platform performs the operation and
// queues the request's completion, which the frame pump runs between frames.
// `src/libeapp/async_file.cpp` implements it over the host file system.
#pragma once

#include "framework/types.h"

#include <cstdint>

namespace holdem::storage {

// Open the file named by `path` for the request. `mode` selects read or write; `reading` is the
// extra argument the four-argument form carries. Answers non-zero when the open was accepted.
[[nodiscard]] uint32_t open(uint32_t mode, GuestAddress path, uint32_t reading,
                            GuestAddress request);
[[nodiscard]] uint32_t open_for_read(uint32_t mode, GuestAddress path, GuestAddress request);

// Perform the request's operation — a read, a seek, or a skip, as its own fields say. Answers
// the number of bytes transferred and queues the request's completion.
[[nodiscard]] uint32_t perform(GuestAddress request);

// Finish with the request's file; the completion is queued as for any other operation.
[[nodiscard]] uint32_t close(GuestAddress request);

// Answer every store call the way the emulator's stubs did — "not ready", so the game never
// writes — for the sake of the recordings in tests/expected/, which were made against that. The
// oracle turns it on (runtime/main.cpp, --emulator-firmware); nothing else should.
void set_store_stubbed(bool stubbed);

// Refuse every transfer *to* a file the way the pinned emulator does by default: nothing is
// written, the operation reports zero bytes and a failed status, and the game goes on without
// its save. The recordings in tests/expected/ were made that way — the emulator's writes are
// behind `EAPP_OP3_WRITES`, off — and with the write allowed to succeed the game does not take
// the same path (PLAN.md, progress log 2026-08-27). The oracle turns this on
// (runtime/main.cpp, --emulator-firmware); a real run keeps its saves.
void set_writes_refused(bool refused);

// The settings store the pause menu writes through. The iPod's semantics here are not fully
// known; these report success, which is what the game does with the answer.
[[nodiscard]] uint32_t store_open(uint32_t mode, GuestAddress path, GuestAddress record);
[[nodiscard]] uint32_t store_write(uint32_t handle, uint32_t bytes, uint32_t count);
[[nodiscard]] uint32_t store_close(uint32_t handle);

}  // namespace holdem::storage
