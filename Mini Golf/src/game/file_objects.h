// The game's file objects (file_objects.cpp), as the save-file records use them.
#pragma once

#include "state.h"

#include <cstdint>

namespace minigolf::game {

uint32_t file_object_acquire(uint32_t name, uint32_t mode);  // 0x18016e38
uint32_t file_object_write(FileObject& object, uint32_t buffer, uint32_t count,
                           uint32_t size);   // 0x18016ae4
void file_object_close(FileObject& object);  // 0x18016dac

uint32_t file_object_written(FileObject& object, uint32_t out);

void file_objects_construct();     // 0x180186cc
void request_records_construct();  // 0x180186f4

}  // namespace minigolf::game
