// Dying with something useful said first.
//
// Split out of each title's `runtime.h` because it is the one runtime service that has nothing to
// do with the guest CPU: no `Cpu&`, no registers, no image. The rest of `runtime.h` — assert
// traps, semihosting, entry tracing — takes a `Cpu&` and stays with the title, whose CPU struct
// differs (Lost models the flag-field `msr` that armcc's soft-float library needs).
#pragma once

namespace ipod {

// Print a message (printf-style), then exit. Used for every condition that means the recomp
// itself is wrong or the game is somewhere it cannot be.
[[noreturn]] void fatal(const char* format, ...) __attribute__((format(printf, 1, 2)));

// A platform may ask to be told before the program dies. On a desktop the message goes to the
// terminal and that is the end of it; a console has no terminal behind it, so the platform needs
// the chance to put the message somewhere the player can actually read it. The handler is called
// after the message has been written, and before the program exits.
void set_fatal_handler(void (*handler)(const char* message));

}  // namespace ipod
