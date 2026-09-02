// Hand-written replacements for ARM C-library routines the recompiler cannot or should not emit.
//
// armcc linked its own C library into the game (memcpy, the division helpers, a small printf, a
// soft-float library). Almost all of it is plain ARM code and is recompiled exactly like game
// code — see tools/recomp/generate.py. The functions here are the exceptions, listed in
// src/runtime/arm_runtime.json so the emitter knows to leave them out. Each takes the guest CPU
// and reads its arguments from r0–r3 under the ARM calling convention, like the recompiled code.
#pragma once

#include "runtime/cpu.h"

namespace minigolf::runtime {

// `_fadd` from armcc's software floating-point library, at 0x18018b88: r0 = r0 + r1 as IEEE
// single-precision. Not recompiled because its exception path reads and writes the CPSR
// (`mrs`/`msr`), which the decoder deliberately does not model. The game calls it from one
// place; host float arithmetic is IEEE round-to-nearest, matching the library's default mode.
void soft_float_add(Cpu& cpu);

// `_ll_udiv` at 0x1800163c: unsigned 64-bit division. Numerator in r0:r1 (low:high), denominator
// in r2:r3; returns the quotient in r0:r1 and the remainder in r2:r3. The signed wrapper at
// 0x18000ba4 is recompiled and calls this. Not recompiled because it selects its unrolled inner
// loop with `add pc, pc, rN, lsl #2/#3` jump tables bounded by arithmetic rather than a `cmp`,
// which the control-flow analysis does not resolve. Division by zero is fatal, where the
// library would have raised its `__rt_div0` trap.
void long_long_udiv(Cpu& cpu);

}  // namespace minigolf::runtime
