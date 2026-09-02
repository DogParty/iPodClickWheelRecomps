// hole_load.cpp, as other files call it.
#pragma once

#include <cstdint>

namespace minigolf::game {

// A transform matrix is sixteen 16.16 words; they live in the hole's packed records, so they are
// read out by address (`matrix_read`) rather than passed as pointers into them.
constexpr uint32_t MATRIX_WORDS = 16;

void matrix_read(uint32_t at, uint32_t (&out)[MATRIX_WORDS]);
void matrix_transform(const uint32_t* matrix, const uint32_t* vector, uint32_t* out);

void course_data_load();

void hole_layout_load();

void course_images_load();

void ground_camera_prepare(uint32_t first);

}  // namespace minigolf::game
