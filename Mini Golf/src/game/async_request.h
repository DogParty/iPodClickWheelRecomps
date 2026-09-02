// The game's AsyncFileIO request objects and its C++ heap operators (async_request.cpp).
#pragma once

#include "records.h"
#include "state.h"

#include <cstdint>

namespace minigolf::game {

uint32_t operator_new(uint32_t size);    // 0x180184cc
void operator_delete(uint32_t pointer);  // 0x180184b4

void async_request_construct(AsyncRequest& request);  // 0x18018458
void async_request_setup_transfer(AsyncRequest& request, uint32_t operation, uint32_t buffer,
                                  uint32_t length, uint32_t callback,
                                  uint32_t context);  // 0x180183cc
void async_request_setup_operation(AsyncRequest& request, uint32_t operation, uint32_t callback,
                                   uint32_t context);  // 0x180182c4 (1), 0x18018338 (2)
void async_request_attach(AsyncRequest& request, Operation& file_object);  // 0x180183b0
void async_request_setup_write(uint32_t request, uint32_t buffer, uint32_t length,
                               uint32_t callback,
                               uint32_t context);  // 0x180182d8
void async_request_setup_read(uint32_t request, uint32_t buffer, uint32_t length, uint32_t callback,
                              uint32_t context);  // 0x1801834c
void async_request_setup_positioned(AsyncRequest& request, uint32_t offset, uint32_t kind,
                                    uint32_t callback, uint32_t context);      // 0x18018300
void operation_construct(Operation& op);                                       // 0x180182a4
uint32_t async_request_issue(uint32_t op, uint32_t request);                   // 0x1801810c
uint32_t operation_close(Operation& op, uint32_t callback, uint32_t context);  // 0x18018078

void simple_file_close(SimpleFile& record);

void simple_file_open(SimpleFile& record, uint32_t mode, uint32_t name);

void simple_file_write(SimpleFile& record, uint32_t bytes, uint32_t count);

uint32_t issue_whole_file(Operation& op, uint32_t mode, uint32_t name, uint32_t buffer,
                          uint32_t length, uint32_t callback,
                          uint32_t context);  // 0x1801812c / 0x180181e8
uint32_t issue_positioned_open(Operation& op, uint32_t mode, uint32_t name, uint32_t reading,
                               uint32_t callback,
                               uint32_t context);  // 0x18017fcc

uint32_t at_exit_register(uint32_t function, uint32_t argument, uint32_t dso);  // 0x18018574

}  // namespace minigolf::game
