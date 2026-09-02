// The ARM-ABI entries of the hand-decompiled functions (`f_<address>`): each reads its
// arguments from the registers and the guest stack and calls the C++ function. They exist for
// the addresses the game stores as function pointers (dispatch.cpp) and, until every direct
// call uses the C++ name, for the callers that still go through the original's address.
#pragma once

#include "runtime/cpu.h"

namespace minigolf::game {

void f_18004b4c(Cpu& cpu);      // hand-decompiled, see src/game/
void f_180068fc(Cpu& cpu);      // hand-decompiled, see src/game/
void f_18006afc(Cpu& cpu);      // hand-decompiled, see src/game/
void f_18006ce4(Cpu& cpu);      // hand-decompiled, see src/game/
void f_1800e644(Cpu& cpu);      // hand-decompiled, see src/game/
void f_18016ca0(Cpu& cpu);      // hand-decompiled, see src/game/
void f_18016e98(Cpu& cpu);      // hand-decompiled, see src/game/
void f_18016ec8(Cpu& cpu);      // hand-decompiled, see src/game/
void f_18016ee8(Cpu& cpu);      // hand-decompiled, see src/game/
void f_180170cc(Cpu& cpu);      // hand-decompiled, see src/game/
void f_18017130(Cpu& cpu);      // hand-decompiled, see src/game/
void f_180172e8(Cpu& cpu);      // hand-decompiled, see src/game/
void f_18017574(Cpu& cpu);      // hand-decompiled, see src/game/
void f_18017a98(Cpu& cpu);      // hand-decompiled, see src/game/
void f_18017d14(Cpu& cpu);      // hand-decompiled, see src/game/
void f_18017f00(Cpu& cpu);      // hand-decompiled, see src/game/
void f_18017f6c(Cpu& cpu);      // hand-decompiled, see src/game/
void f_18018660(Cpu& /*cpu*/);  // hand-decompiled, see src/game/
void f_180188bc(Cpu& /*cpu*/);  // hand-decompiled, see src/game/
void f_180188c0(Cpu& /*cpu*/);  // hand-decompiled, see src/game/
void f_1801891c(Cpu& cpu);      // hand-decompiled, see src/game/

}  // namespace minigolf::game
