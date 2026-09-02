// The save-file records and the score-file loader (save_files.cpp).
#pragma once

#include "records.h"

#include <cstdint>

namespace minigolf::game {

uint32_t file_record_open(uint32_t name, uint32_t write);  // 0x1800753c
uint32_t file_record_transfer(FileRecord& record, uint32_t buffer,
                              uint32_t bytes);  // 0x18007620
void file_record_close(FileRecord& record);     // 0x180074d4
uint32_t score_file_begin(uint32_t state);      // 0x18010860
void course_sounds_load();                      // 0x18004660 (sound_bank.cpp)
uint32_t renderer_create();                     // 0x18008c78 (gl_state.cpp)

}  // namespace minigolf::game
