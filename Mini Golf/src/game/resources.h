// The resource packs and images (resources.cpp), as other files call them.
#pragma once

#include "records.h"
#include "state.h"

#include <cstdint>

namespace minigolf::game {

uint32_t resource_load(PackRecord& pack_at, uint32_t id, uint32_t destination,
                       uint32_t limit);  // 0x18008868
uint32_t image_from_resource(ImageRecord& image, uint32_t variant, PackRecord& pack_at, uint32_t id,
                             uint32_t texture);                       // 0x18008fcc
uint32_t pack_open(uint32_t name, uint32_t unused, uint32_t suffix);  // 0x18008664
void files_release(uint32_t all);                                     // 0x1800409c
uint32_t tracked_allocate(uint32_t bytes);                            // 0x1800fe28
void title_pack_open();                                               // 0x1800fd0c

// What an array's elements are built with: the original passed the constructor's address.
using ElementConstructor = void (*)(uint32_t element);

uint32_t array_construct(uint32_t memory, uint32_t allocator, uint32_t size, uint32_t header,
                         uint32_t count, uint32_t destructor, ElementConstructor constructor,
                         uint32_t clear);

void texture_release(ImageRecord& image);

void tracked_free(uint32_t memory);

uint32_t image_apply(ImageRecord& image, uint32_t variant, PackRecord& pack_at, uint32_t id);

uint32_t resource_size(PackRecord& pack_at, uint32_t id);

void ground_tile_blit(uint32_t pixels, uint32_t x, uint32_t y);

uint32_t resource_open(PackRecord& pack_at, uint32_t id, uint32_t destination, uint32_t allocate);

void object_rect_a_set(RectObject& object, uint32_t x, uint32_t y, uint32_t width,
                       uint32_t height);  // 0x18008db4
void object_rect_b_set(RectObject& object, uint32_t x, uint32_t y, uint32_t width,
                       uint32_t height);  // 0x18008dcc

uint32_t texture_from_pixels(ImageRecord& image, uint32_t variant, uint32_t width, uint32_t height,
                             uint32_t pixels, uint32_t texture);  // 0x180090c8

}  // namespace minigolf::game
