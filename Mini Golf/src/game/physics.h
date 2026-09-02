// physics.cpp, as other files call it.
#pragma once

#include <cstdint>

namespace minigolf::game {

uint32_t integer_sqrt(uint32_t n);

int32_t power_meter_value();

uint32_t ball_step();

uint32_t point_blocked(int32_t x, int32_t y);

void trail_reset();

uint32_t ball_move(uint32_t shift);

int32_t sine_degrees(int32_t degrees);

}  // namespace minigolf::game
