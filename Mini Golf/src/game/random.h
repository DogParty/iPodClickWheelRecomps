// The game's random number generator (random.cpp).
#pragma once

#include <cstdint>

namespace minigolf::game {

// Seed the generator (0x1800fddc).
void random_seed(uint32_t seed);

uint32_t random_next(uint32_t object, uint32_t modulus);

uint32_t random_create(uint32_t seed);

}  // namespace minigolf::game
