// The sound bank loader (sound_bank.cpp), as other files call it.
#pragma once

#include <cstdint>

namespace minigolf::game {

constexpr uint32_t SOUND_BANK = 0x1804'1418;  // the one bank

void sound_bank_construct(uint32_t bank);  // 0x18017eb8
void course_sounds_load();                 // 0x18004660

}  // namespace minigolf::game
