// The click-wheel input layer: wheel position, button edges, and the firmware's request objects.
#pragma once

#include "state.h"

#include <cstdint>

namespace minigolf::game {

// Store the wheel position the firmware reported (0x180135c8).
void wheel_position_update(uint32_t position);

// Publish this frame's button flags and event list to the game logic (0x18013fac).
void input_snapshot_store(uint32_t flags, uint32_t event_list);

// Copy the snapshot's four words to `destination` (0x18013690), and clear its flags
// (0x180142bc) or its first word (0x18014ccc) once the update has consumed them.
void input_snapshot_read(InputSnapshot& destination);
void input_snapshot_clear_flags();
void input_snapshot_clear_movement();

// The step writes its results back into the snapshot: the wheel movement (0x18014bec) and the
// filtered button flags (0x18013f9c). 0x18014bfc copies two firmware words into a step copy.
void wheel_movement_apply(int32_t movement);
void buttons_apply(uint32_t flags);
void prepare_step_input(InputSnapshot& copy, uint32_t word_a, uint32_t word_b);

// Turn the firmware's button events into flag edges (0x18012d50). `pressed_mask` selects the
// buttons whose press events are also forwarded to InputEvents #1.
// Takes the flags word and answers what it has become, rather than writing through a reference:
// the caller's copy is a field of a packed structure, and a compiler is right to refuse to bind a
// reference to one (the aarch64 build does).
[[nodiscard]] uint32_t dispatch_buttons(uint32_t event_list, uint32_t button_flags, uint32_t clock,
                                        uint32_t pressed_mask);

// Mark a request free and run its completion callback if it has one (0x18018374).
void release_request(EventNode& request);

// Release every request on a list, unlinking each first (0x1801842c).
void release_completed_requests(uint32_t list);

void input_gather();

}  // namespace minigolf::game
