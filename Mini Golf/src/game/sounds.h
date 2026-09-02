// The slots of loaded sounds (sounds.cpp), as the bank loader uses them.
#pragma once

#include <cstdint>

namespace minigolf::game {

// The five slots of loaded sounds: whether a slot is in use, and a framework sound handle per
// index (-1 when empty). Host state: the original's tables (0x1801bdcc, 0x18040ee8) were read
// by nothing else and never handed to a framework.
struct SoundSlots {
    static constexpr uint32_t COUNT = 5, INDEX_LIMIT = 64;
    uint8_t enabled[COUNT];
    uint32_t handle[COUNT][INDEX_LIMIT];
};
SoundSlots& sound_slots();

uint32_t sound_slot_create(uint32_t slot, uint32_t index);   // 0x180121f4
uint32_t sound_slot_present(uint32_t slot, uint32_t index);  // 0x1801224c
void sound_slot_release(uint32_t slot, uint32_t index);      // 0x1801051c

uint32_t battery_query(uint32_t low_out, uint32_t critical_out, uint32_t spare_out);

}  // namespace minigolf::game
