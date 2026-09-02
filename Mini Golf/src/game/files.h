// The game's file service: ten operation slots over AsyncFileIO (files.cpp).
#pragma once

#include "state.h"

#include <cstdint>

namespace minigolf::game {

// The service object, created on first use (0x180170fc).
uint32_t file_service_get();
uint32_t file_service_construct(FileService& service);  // 0x180178e0
void file_slot_construct(uint32_t slot);                // 0x180177fc
uint32_t file_write_begin(FileService& service, uint32_t handle, uint32_t buffer,
                          uint32_t length);  // 0x18017000
uint32_t file_read_begin(FileService& service, uint32_t handle, uint32_t buffer,
                         uint32_t length);                         // 0x180173bc
uint32_t file_close_begin(FileService& service, uint32_t handle);  // 0x18017320
uint32_t file_open_positioned(FileService& service, uint32_t mode, uint32_t name, uint32_t kind,
                              uint32_t callback, uint32_t context);               // 0x18016f4c
void file_transfer_completed(uint32_t service, uint32_t request, uint32_t slot);  // 0x18017574
void file_close_completed(uint32_t slot);                                         // 0x180174a8
void file_open_completed(uint32_t request, uint32_t slot);                        // 0x18017250

// Fill in a request: mode, name, buffer, and the three words the caller passes on the stack
// (0x18017864). Returns the request.
void file_request_prepare(FileRequest& request, uint32_t mode, uint32_t name, uint32_t buffer,
                          uint32_t offset, uint32_t kind, uint32_t length);

// Start an operation from a slot; the slot's id is the handle, or -1 when none is free or the
// framework refused (0x1801761c read, 0x180176fc write).
uint32_t file_begin(FileService& service, uint32_t request, uint32_t argument_a,
                    uint32_t argument_b, bool write);

// Progress of a handle: finished (0x18017554), bytes transferred (0x180177dc), release
// (0x18017510).
uint32_t file_finished(FileService& service, uint32_t handle);
uint32_t file_status(FileService& service, uint32_t handle);
void file_close(FileService& service, uint32_t handle);

}  // namespace minigolf::game
