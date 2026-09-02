// The game's file service. See files.h and the file_service / file_slot layouts in game_state.h.
//
// Every save-game read and write goes through here. A slot is taken from a free list, the
// caller's request is copied into it, and one of three still-recompiled issuers hands an
// AsyncFileIO request to the framework with a completion callback that later fills in the
// slot's status and done flag. Handles are slot indices.
#include "files.h"

#include "async_request.h"
#include "calling.h"
#include "game_state.h"
#include "libc.h"
#include "records.h"
#include "resources.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "shims.h"
#include "state.h"

namespace minigolf::game {

namespace {

// Still recompiled, named by their use here (inferred).

constexpr uint32_t REQUEST_COPY_SIZE = 0x114;
constexpr uint32_t TRANSFER_COMPLETION = 0x1801'7574, CLOSE_COMPLETION = 0x1801'70cc;

uint32_t slot_address(FileService& service, uint32_t handle) {
    return address_of(service) + handle * file_service::SLOT_SIZE;
}

void slot_free(FileService& service, uint32_t slot) {
    as_file_slot(slot).next_free = service.free_list;
    service.free_list = slot;
}

}  // namespace

uint32_t file_service_get() {
    if (guest<uint32_t>(FILE_SERVICE_POINTER) == 0) {
        guest<uint32_t>(FILE_SERVICE_POINTER) =
            file_service_construct(as_file_service(operator_new(FILE_SERVICE_SIZE)));
    }
    return guest<uint32_t>(FILE_SERVICE_POINTER);
}

void file_request_prepare(FileRequest& request, uint32_t mode, uint32_t name, uint32_t buffer,
                          uint32_t offset, uint32_t kind, uint32_t length) {
    request.mode = static_cast<uint8_t>(mode);
    request.buffer = buffer;
    request.kind = static_cast<uint8_t>(kind);
    request.offset = offset;
    request.length = length;
    // strncpy of the name into the request's name field.
    uint32_t i = 0;
    for (; i < file_request::NAME_CAPACITY; ++i) {
        const uint32_t c = guest<uint8_t>(name + i);
        request.name[i] = static_cast<uint8_t>(c);
        if (c == 0) {
            break;
        }
    }
    for (++i; i < file_request::NAME_CAPACITY; ++i) {
        request.name[i] = static_cast<uint8_t>(0);
    }
    request.name[file_request::NAME_CAPACITY - 1] = static_cast<uint8_t>(0);
}

uint32_t file_begin(FileService& service, uint32_t request, uint32_t argument_a,
                    uint32_t argument_b, bool write) {
    GuestScratch frame(4 * 10);
    const uint32_t slot = service.free_list;
    if (slot == 0) {
        return FILE_HANDLE_INVALID;
    }
    service.free_list = as_file_slot(slot).next_free;
    as_file_slot(slot).next_free = 0;
    const uint32_t handle = as_file_slot(slot).id;
    as_file_slot(slot).active = static_cast<uint8_t>(1);
    as_file_slot(slot).reading = static_cast<uint8_t>(write ? 0 : 1);
    as_file_slot(slot).byte_06 = static_cast<uint8_t>(write ? 0 : 1);
    as_file_slot(slot).status = 0;
    libc::memory_copy(slot + file_slot::REQUEST, request, REQUEST_COPY_SIZE);
    as_file_slot(slot).done = static_cast<uint8_t>(0);
    as_file_slot(slot).arguments[0] = argument_a;
    as_file_slot(slot).arguments[1] = argument_b;

    FileRequest& copy = as_file_request(slot + file_slot::REQUEST);
    const uint32_t kind = static_cast<uint32_t>(copy.kind);
    const bool whole_file = (kind == 0 || kind == 1) && copy.offset == 0;
    uint32_t accepted = 0;
    if (whole_file) {
        accepted = issue_whole_file(as_operation(slot + file_slot::ASYNC_REQUEST),
                                    static_cast<uint32_t>(copy.mode),
                                    field_address(copy, offsetof(FileRequest, name)), copy.buffer,
                                    copy.length, FILE_READ_COMPLETION, slot);
    } else {
        accepted = issue_positioned_open(
            as_operation(slot + file_slot::ASYNC_REQUEST), static_cast<uint32_t>(copy.mode),
            field_address(copy, offsetof(FileRequest, name)),
            static_cast<uint32_t>(as_file_slot(slot).reading), FILE_OTHER_COMPLETION, slot);
    }
    if (accepted == 0) {  // the framework refused: give the slot back
        slot_free(service, slot);
        return FILE_HANDLE_INVALID;
    }
    return handle;
}

uint32_t file_finished(FileService& service, uint32_t handle) {
    return handle < file_service::SLOT_COUNT ? as_file_slot(slot_address(service, handle)).done : 1;
}

uint32_t file_status(FileService& service, uint32_t handle) {
    return handle < file_service::SLOT_COUNT ? as_file_slot(slot_address(service, handle)).status
                                             : 0;
}

void file_close(FileService& service, uint32_t handle) {
    if (handle >= file_service::SLOT_COUNT) {
        return;
    }
    const uint32_t slot = slot_address(service, handle);
    if (static_cast<uint32_t>(as_file_slot(slot).busy) != 0) {
        assert_trap(0x18017530u);
    }
    if (as_file_slot(slot).next_free == 0) {
        as_file_slot(slot).done = static_cast<uint8_t>(1);
        slot_free(service, slot);
    }
}

// 0x180177fc — a slot as built: inactive, empty, its two request records constructed.
void file_slot_construct(uint32_t slot) {
    as_file_slot(slot).id = 0xffff'ffffu;
    as_file_slot(slot).active = 0;
    as_file_slot(slot).reading = 0;
    as_file_slot(slot).byte_06 = 0;
    as_file_slot(slot).busy = 0;
    FileRequest& request = as_file_slot(slot).request;
    request.mode = 0;
    request.buffer = 0;
    request.kind = 0;
    request.offset = 0;
    request.length = 0;
    request.name[0] = 0;
    as_file_slot(slot).status = 0;
    as_file_slot(slot).done = static_cast<uint8_t>(0);
    async_request_construct(as_request(slot + file_slot::TRANSFER_REQUEST));
    as_file_slot(slot).arguments[0] = 0;
    operation_construct(as_operation(slot + file_slot::ASYNC_REQUEST));
    as_file_slot(slot).next_free = 0;
}

// 0x180178e0 — the service: ten slots constructed and chained free, the first as the head.
uint32_t file_service_construct(FileService& service) {
    array_construct(address_of(service), 0, file_service::SLOT_SIZE, 0, file_service::SLOT_COUNT, 0,
                    file_slot_construct, 0);
    for (uint32_t i = 0; i < file_service::SLOT_COUNT; ++i) {
        const uint32_t slot = address_of(service) + i * file_service::SLOT_SIZE;
        as_file_slot(slot).id = i;
        as_file_slot(slot).next_free = slot + file_service::SLOT_SIZE;
    }
    service.word_f24 = 0;
    service.free_list = address_of(service);
    return address_of(service);
}

namespace {

// A slot by handle, if the handle is good and the slot is open and idle.
uint32_t slot_ready(FileService& service, uint32_t handle) {
    if (static_cast<int32_t>(handle) < 0 || handle >= file_service::SLOT_COUNT) {
        assert_trap(0x180173e0u);
    }
    const uint32_t slot = slot_address(service, handle);
    if (static_cast<uint32_t>(as_file_slot(slot).busy) == 0) {
        return 0;
    }
    if (file_finished(service, handle) == 0) {
        return 0;
    }
    if (as_file_slot(slot).next_free != 0 || service.free_list == slot) {
        return 0;
    }
    return slot;
}

uint32_t transfer_begin(FileService& service, uint32_t handle, uint32_t buffer, uint32_t length,
                        bool write) {
    const uint32_t slot = slot_ready(service, handle);
    if (slot == 0) {
        return 0;
    }
    as_file_slot(slot).byte_06 = static_cast<uint8_t>(write ? 1 : 0);
    as_file_slot(slot).status = 0;
    as_file_slot(slot).request.buffer = buffer;
    as_file_slot(slot).request.length = length;
    as_file_slot(slot).done = static_cast<uint8_t>(0);
    (write ? async_request_setup_write : async_request_setup_read)(
        slot + file_slot::TRANSFER_REQUEST, buffer, length, TRANSFER_COMPLETION, slot);
    return async_request_issue(slot + file_slot::ASYNC_REQUEST, slot + file_slot::TRANSFER_REQUEST);
}

}  // namespace

// 0x18017000 / 0x180173bc — a write / a read of `length` bytes through an open, idle slot;
// the completion the slot was opened with hears of it. Returns what the framework answered,
// 0 if the slot was not ready or the framework refused.
uint32_t file_write_begin(FileService& service, uint32_t handle, uint32_t buffer, uint32_t length) {
    return transfer_begin(service, handle, buffer, length, true);
}

uint32_t file_read_begin(FileService& service, uint32_t handle, uint32_t buffer, uint32_t length) {
    return transfer_begin(service, handle, buffer, length, false);
}

// 0x18017320 — close an open slot's file; the slot is released when the close completes.
uint32_t file_close_begin(FileService& service, uint32_t handle) {
    const uint32_t slot = slot_ready(service, handle);
    if (slot == 0) {
        return 0;
    }
    as_file_slot(slot).done = static_cast<uint8_t>(0);
    return operation_close(as_operation(slot + file_slot::ASYNC_REQUEST), CLOSE_COMPLETION, slot);
}

// 0x18017574 — a transfer's completion: the bytes moved into the slot's status; an active
// slot closes its file next, an inactive one is done and tells its callback.
void file_transfer_completed(uint32_t service, uint32_t request, uint32_t slot) {
    (void)service;
    if (slot == 0 || slot + file_slot::TRANSFER_REQUEST != request) {
        assert_trap(0x180175c8u);
    }
    as_file_slot(slot).status = as_request(slot + file_slot::TRANSFER_REQUEST).size_result;
    if (static_cast<uint32_t>(as_file_slot(slot).active) != 0) {
        operation_close(as_operation(slot + file_slot::ASYNC_REQUEST), CLOSE_COMPLETION, slot);
        return;
    }
    as_file_slot(slot).done = static_cast<uint8_t>(1);
    const uint32_t callback = as_file_slot(slot).arguments[0];
    if (callback != 0) {
        call_indirect(callback, {as_file_slot(slot).id, as_file_slot(slot).status,
                                 slot + file_slot::REQUEST, as_file_slot(slot).arguments[1]});
    }
}

// 0x180174a8 — a close's completion: the slot is no longer busy and is done; its callback
// gets the status (or 0 for a slot that was never active).
void file_close_completed(uint32_t slot) {
    if (slot == 0) {
        assert_trap(0x180174b4u);
    }
    as_file_slot(slot).busy = static_cast<uint8_t>(0);
    as_file_slot(slot).done = static_cast<uint8_t>(1);
    const uint32_t callback = as_file_slot(slot).arguments[0];
    if (callback == 0) {
        return;
    }
    const uint32_t status =
        static_cast<uint32_t>(as_file_slot(slot).active) != 0 ? as_file_slot(slot).status : 0;
    call_indirect(callback, {as_file_slot(slot).id, status, slot + file_slot::REQUEST,
                             as_file_slot(slot).arguments[1]});
}

// 0x18017250 — an open's completion for a positioned slot: an active slot goes straight on
// to its transfer; an inactive one is done, and its callback hears so with a status of 0.
void file_open_completed(uint32_t request, uint32_t slot) {
    {
        if (slot == 0 || slot + file_slot::TRANSFER_REQUEST != request) {
            assert_trap(0x18017268u);
        }
        if (static_cast<uint32_t>(as_file_slot(slot).active) != 0) {
            const bool write = static_cast<uint32_t>(as_file_slot(slot).byte_06) != 0;
            (write ? async_request_setup_write : async_request_setup_read)(
                slot + file_slot::TRANSFER_REQUEST, as_file_slot(slot).request.buffer,
                as_file_slot(slot).request.length, TRANSFER_COMPLETION, slot);
            async_request_issue(slot + file_slot::ASYNC_REQUEST,
                                slot + file_slot::TRANSFER_REQUEST);
            return;
        }
        as_file_slot(slot).done = static_cast<uint8_t>(1);
    }
    const uint32_t callback = as_file_slot(slot).arguments[0];
    if (callback != 0) {
        call_indirect(callback, {as_file_slot(slot).id, 0, slot + file_slot::REQUEST,
                                 as_file_slot(slot).arguments[1]});
    }
}

// 0x18016f4c — open a file by name for positioned transfers: a free slot takes the mode,
// name and kind, and the open is issued with the given completion. Returns the handle, -1
// when no slot is free or the framework refused.
uint32_t file_open_positioned(FileService& service, uint32_t mode, uint32_t name, uint32_t kind,
                              uint32_t callback, uint32_t context) {
    GuestScratch frame(4 * 10);
    const uint32_t slot = service.free_list;
    if (slot == 0) {
        return FILE_HANDLE_INVALID;
    }
    service.free_list = as_file_slot(slot).next_free;
    as_file_slot(slot).next_free = 0;
    const uint32_t handle = as_file_slot(slot).id;
    as_file_slot(slot).active = static_cast<uint8_t>(0);
    as_file_slot(slot).reading = static_cast<uint8_t>(kind);
    as_file_slot(slot).status = 0;
    as_file_slot(slot).request.mode = static_cast<uint8_t>(mode);
    libc::string_copy_bounded(slot + file_slot::REQUEST + file_request::NAME, name,
                              file_request::NAME_CAPACITY);
    as_file_slot(slot).request.kind = 0;
    as_file_slot(slot).request.buffer = 0;
    as_file_slot(slot).request.offset = 0;
    as_file_slot(slot).request.length = 0;
    as_file_slot(slot).done = static_cast<uint8_t>(0);
    as_file_slot(slot).arguments[0] = callback;
    as_file_slot(slot).arguments[1] = context;
    const uint32_t accepted = issue_positioned_open(
        as_operation(slot + file_slot::ASYNC_REQUEST),
        static_cast<uint32_t>(as_file_slot(slot).request.mode),
        slot + file_slot::REQUEST + file_request::NAME,
        static_cast<uint32_t>(as_file_slot(slot).reading), FILE_OTHER_COMPLETION, slot);
    if (accepted == 0) {
        slot_free(service, slot);
        return FILE_HANDLE_INVALID;
    }
    return handle;
}

// --- shims -----------------------------------------------------------------------------------

// (service, request, slot) — the slot-level transfer completion
void f_18017574(Cpu& cpu) {
    const uint32_t request = cpu.r[0], slot = cpu.r[1];
    uint32_t service = 0;
    {
        service = file_service_get();
    }
    file_transfer_completed(service, request, slot);
}

void f_180170cc(Cpu& cpu) {
    const uint32_t slot = cpu.r[3];
    file_service_get();
    file_close_completed(slot);
}

void f_18016ec8(Cpu& cpu) {
    const uint32_t request = cpu.r[0], slot = cpu.r[1];
    {
        file_service_get();
    }
    file_open_completed(request, slot);
}

}  // namespace minigolf::game
