// cpu.h — the shared core's, brought into this title's namespace.
//
// The definitions live in `common/src/ipod/runtime/cpu.h` and are compiled once for every
// title. This header exists so that nothing else in this tree has to know that: the include
// path and the qualified names callers already write are unchanged, and the names arrive by
// `using` rather than by alias, so this namespace can still hold what is genuinely this
// title's.
//
// See ../../../common/README.md.
#pragma once

#include "ipod/runtime/cpu.h"

namespace minigolf {
using ::ipod::bit;
using ::ipod::CPSR_MODE_BITS;
using ::ipod::Cpu;
using ::ipod::registers;
using ::ipod::SP;
using ::ipod::LR;
using ::ipod::PC;
using ::ipod::Shift;
using ::ipod::Shifted;
using ::ipod::asr;
using ::ipod::ror;
using ::ipod::rrx;
using ::ipod::shift_imm;
using ::ipod::shift_reg;
using ::ipod::lsl_reg;
using ::ipod::lsr_reg;
using ::ipod::asr_reg;
using ::ipod::ror_reg;
using ::ipod::add_with_carry;
using ::ipod::add_flags;
using ::ipod::adc_flags;
using ::ipod::sub_flags;
using ::ipod::sbc_flags;
using ::ipod::logic_flags;
using ::ipod::mul_flags;
using ::ipod::multiply_long;
using ::ipod::signed_product;
using ::ipod::accumulator;
using ::ipod::umull;
using ::ipod::smull;
using ::ipod::umlal;
using ::ipod::smlal;
using ::ipod::cond_eq;
using ::ipod::cond_ne;
using ::ipod::cond_cs;
using ::ipod::cond_cc;
using ::ipod::cond_mi;
using ::ipod::cond_pl;
using ::ipod::cond_vs;
using ::ipod::cond_vc;
using ::ipod::cond_hi;
using ::ipod::cond_ls;
using ::ipod::cond_ge;
using ::ipod::cond_lt;
using ::ipod::cond_gt;
using ::ipod::cond_le;
}  // namespace minigolf
