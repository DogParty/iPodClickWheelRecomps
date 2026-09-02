// Types shared by the framework interfaces (src/framework/).
//
// The game asks the platform to do things through the headers in this directory: draw, play a
// sound, read a file, poll the wheel, ask the device something. `src/libeapp/` implements them
// as the iPod's own frameworks behaved; a test can link a different implementation instead.
#pragma once

#include <cstdint>

namespace ipod {

// An address in the emulated address space. Still how the game hands buffers to the platform:
// vertex arrays, textures, file buffers and matrices live in guest memory because the recompiled
// code and the frameworks both address them there. Every one of these is a place where a real
// pointer or span will go once the buffer it names is host memory.
using GuestAddress = uint32_t;

// A 32-bit IEEE float in the form the game computes with. The iPod's ABI passes floats in
// integer registers, and the game's own float helpers produce and consume these bit patterns,
// so a value on its way to the platform is still bits at the boundary.
using Float32Bits = uint32_t;

}  // namespace ipod
