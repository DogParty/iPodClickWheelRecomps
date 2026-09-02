// The game's AsyncFileIO request objects: the class the file service's slots issue operations
// through, and the completion path back.
//
// Three layers sit between a file slot (files.cpp) and the framework:
//
//   AsyncRequest  (0x3c bytes, heap)  the object AsyncFileIO works on: operation, buffer and
//                                     length, status, and the completion callback + context.
//   Operation     (at slot + 0x16c)   the slot's record of the request in flight: state, the
//                                     request's result, and the slot-level callback + context.
//   completion    (0x18017f6c ...)    the AsyncRequest's callback: copies status to the
//                                     Operation, frees the request, and calls the slot callback.
//
// Layouts are in game_state.h (`async_request`, `operation`).
#include "async_request.h"

#include "calling.h"
#include "files.h"
#include "framework/storage.h"
#include "game_state.h"
#include "libc.h"
#include "records.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "shims.h"
#include "state.h"

namespace minigolf::game {

namespace {

constexpr uint32_t ASYNC_REQUEST_SIZE = 0x3c;
constexpr uint32_t OPERATION_WHOLE_FILE = 6;           // the AsyncFileIO operation the issuers use
constexpr uint32_t DEFAULT_NEW_HANDLER = 0x1800'1b88;  // armcc's, raises "Out of heap memory"
constexpr uint32_t HEAP_DESCRIPTOR_NEW_HANDLER = 0x34;

// Still recompiled: the heap descriptor accessor the C library uses.
// Still recompiled: the follow-on operations an open leads to (inferred from their set-up).
constexpr uint32_t REQUEST_COMPLETION = 0x1801'7f6c;  // every request's own completion
constexpr uint32_t OPEN_THEN_CLOSE_COMPLETION = 0x1801'70cc,
                   POSITIONED_READ_COMPLETION = 0x1801'6ec8, TRANSFER_COMPLETION = 0x1801'7574;

}  // namespace

// 0x180184cc — `operator new`: allocate, and on failure call the installed new handler and
// try again, as the C++ runtime specifies. A zero size allocates one byte.
uint32_t operator_new(uint32_t size) {
    const uint32_t wanted = size == 0 ? 1 : size;
    for (;;) {
        const uint32_t memory = libc::heap_allocate(wanted);  // malloc
        if (memory != 0) {
            return memory;
        }
        uint32_t handler = guest<uint32_t>(libc::RUNTIME_DESCRIPTOR + HEAP_DESCRIPTOR_NEW_HANDLER);
        if (handler == 0) {
            handler = DEFAULT_NEW_HANDLER;
        }
        call_indirect(handler);
    }
}

// 0x180184b4 — `operator delete`.
void operator_delete(uint32_t pointer) {
    libc::heap_free(pointer);  // free
}

// 0x18018458 — construct a request: everything clear, the three result words -1.
constexpr uint32_t NO_RESULT = 0xffff'ffffu;  // the three result words start unset

void async_request_construct(AsyncRequest& request) {
    request = AsyncRequest{};
    request.byte_10 = static_cast<uint8_t>(1);
    request.result = NO_RESULT;
    request.file_handle = NO_RESULT;
    request.word_30 = NO_RESULT;
}

// The common tail of the three set-up methods: fresh results, stage 1, and the callback.
void async_request_arm(AsyncRequest& request, uint32_t callback, uint32_t context) {
    request.status = 0;
    request.size_result = 0;
    request.result = 0xffff'ffffu;
    request.stage = static_cast<uint8_t>(1);
    request.callback = callback;
    request.context = context;
}

// 0x180183cc — set up a whole-buffer transfer: (request, operation, buffer, length; stack:
// callback, context).
void async_request_setup_transfer(AsyncRequest& request, uint32_t operation, uint32_t buffer,
                                  uint32_t length, uint32_t callback, uint32_t context) {
    request.state = static_cast<uint8_t>(operation);
    request.file_object = 0;
    request.seek_offset = 0;
    request.byte_10 = static_cast<uint8_t>(1);
    request.buffer = buffer;
    request.length = length;
    async_request_arm(request, callback, context);
}

// 0x180182d8 / 0x1801834c — a write (operation 3) / a read (operation 4) of `length` bytes
// from or into `buffer`: (request, buffer, length, callback; stack: context).
void async_request_setup_write(uint32_t request, uint32_t buffer, uint32_t length,
                               uint32_t callback, uint32_t context) {
    async_request_setup_transfer(as_request(request), 3, buffer, length, callback, context);
}

void async_request_setup_read(uint32_t request, uint32_t buffer, uint32_t length, uint32_t callback,
                              uint32_t context) {
    async_request_setup_transfer(as_request(request), 4, buffer, length, callback, context);
}

// 0x180182c4 (operation 1) and 0x18018338 (operation 2): set up a transfer-less request.
void async_request_setup_operation(AsyncRequest& request, uint32_t operation, uint32_t callback,
                                   uint32_t context) {
    request.state = static_cast<uint8_t>(operation);
    request.file_object = 0;
    async_request_arm(request, callback, context);
}

// 0x180183b0 — point the request at the file object (the Operation record) and take its
// handle; stage 2.
void async_request_attach(AsyncRequest& request, Operation& file_object) {
    request.file_object = address_of(file_object);
    request.file_handle = file_object.file_handle;
    request.stage = static_cast<uint8_t>(2);
}

namespace {}  // namespace

// Start a whole-file read or write through a new request. Returns the framework's answer
// (non-zero on acceptance); on refusal the operation is cleared and the request freed.
uint32_t issue_whole_file(Operation& op, uint32_t mode, uint32_t name, uint32_t buffer,
                          uint32_t length, uint32_t callback, uint32_t context) {
    if (static_cast<uint32_t>(op.state) != 0) {
        return 0;  // one operation at a time
    }
    const uint32_t request = operator_new(ASYNC_REQUEST_SIZE);
    async_request_construct(as_request(request));
    if (request == 0) {
        return 0;
    }
    op.callback = callback;
    op.context = context;
    async_request_setup_transfer(as_request(request), OPERATION_WHOLE_FILE, buffer, length,
                                 REQUEST_COMPLETION, 0);
    async_request_attach(as_request(request), op);
    const uint32_t accepted = storage::open_for_read(mode, name, request);
    if (accepted != 0) {
        op.state = static_cast<uint8_t>(4);
    } else {
        op.callback = 0;
        op.context = 0;
        operator_delete(request);
    }
    return accepted;
}

namespace {

// After a request completes: clear the operation and hand (op, status, size) to its callback.
void operation_finish(Operation& op, uint32_t status, uint32_t size, uint32_t result,
                      bool state_from_status) {
    if (state_from_status) {
        op.state = static_cast<uint8_t>(status == 0 ? 2 : 0);
    } else {
        op.result = result;
        op.state = static_cast<uint8_t>(0);
    }
    const uint32_t callback = op.callback;
    const uint32_t context = op.context;
    op.callback = 0;
    op.context = 0;
    if (callback != 0) {
        call_indirect(callback,
                      {address_of(op), status, size, context});  // a tail call in the original
    }
}

}  // namespace

// 0x1801812c / 0x180181e8 — whole-file read / write: (op, mode, name, buffer; stack: length,
// callback, context).
// 0x18018300 — set up a positioned request (operation 5) at `offset` of `kind`:
// (request, offset, kind, callback; stack: -, context). The context comes from the second
// stack word, which in every caller is the saved slot pointer.
void async_request_setup_positioned(AsyncRequest& request, uint32_t offset, uint32_t kind,
                                    uint32_t callback, uint32_t context) {
    request.state = static_cast<uint8_t>(5);
    request.seek_offset = offset;
    request.file_object = 0;
    request.byte_10 = static_cast<uint8_t>(kind);
    request.buffer = 0;
    request.length = 0;
    request.status = 0;
    request.size_result = 0;
    request.result = 0xffff'ffffu;
    request.stage = static_cast<uint8_t>(1);
    request.callback = callback;
    request.context = context;
}

// 0x180182a4 — an operation record as built: no handle, idle, no result, no callback.
void operation_construct(Operation& op) {
    op.file_handle = 0xffff'ffffu;
    op.state = static_cast<uint8_t>(0);
    op.callback = 0;
    op.result = 0xffff'ffffu;
    op.context = 0;
}

// 0x1801810c — hand a request to the framework against an operation's file (a tail call).
uint32_t async_request_issue(uint32_t op, uint32_t request) {
    async_request_attach(as_request(request), as_operation(op));
    return storage::perform(request);
}

// 0x18018078 — close an operation's file: a fresh request for the close, with the
// completion the caller names; 0 when the operation is not open or the framework refused.
uint32_t operation_close(Operation& op, uint32_t callback, uint32_t context) {
    if (static_cast<uint32_t>(op.state) != 2) {
        return 0;
    }
    const uint32_t request = operator_new(ASYNC_REQUEST_SIZE);
    async_request_construct(as_request(request));
    if (request == 0) {
        return 0;
    }
    op.callback = callback;
    op.context = context;
    async_request_setup_operation(as_request(request), 2, REQUEST_COMPLETION, 0);
    async_request_attach(as_request(request), op);
    const uint32_t accepted = storage::close(request);
    if (accepted != 0) {
        op.state = static_cast<uint8_t>(3);
        return accepted;
    }
    op.callback = 0;
    op.context = 0;
    operator_delete(request);
    return 0;
}

// 0x18015e58 / 0x18015e2c / 0x18015e8c — the simple, synchronous file calls the save and the
// statistics use: open (mode, name) into a two-word record, write from it, close it.
void simple_file_open(SimpleFile& record, uint32_t mode, uint32_t name) {
    record.handle = 0xffff'ffffu;
    record.status = 8;
    record.status = storage::store_open(mode, name, address_of(record));
}

void simple_file_write(SimpleFile& record, uint32_t bytes, uint32_t count) {
    if (record.status == 0 || record.status == 5) {
        record.status = storage::store_write(record.handle, bytes, count);
    }
}

void simple_file_close(SimpleFile& record) {
    (void)storage::store_close(record.handle);  // the original did not check either
}

// 0x18017fcc — open for a positioned transfer: (op, mode, name, reading; stack: callback,
// context). The request's completion is 0x18017f00, which records the file handle.
// 0x18017fcc — open a file by name on an idle operation: a fresh request for the open, whose
// completion (0x18017f00) will hear of it; `callback` and `context` are the operation's own.
// Returns what the framework answered, 0 when the operation is busy or there is no memory.
uint32_t issue_positioned_open(Operation& op, uint32_t mode, uint32_t name, uint32_t reading,
                               uint32_t callback, uint32_t context) {
    if (static_cast<uint32_t>(op.state) != 0) {
        return 0;
    }
    const uint32_t request = operator_new(ASYNC_REQUEST_SIZE);
    async_request_construct(as_request(request));
    if (request == 0) {
        return 0;
    }
    op.callback = callback;
    op.context = context;
    async_request_setup_operation(as_request(request), 1, 0x18017f00u, 0);
    async_request_attach(as_request(request), op);
    const uint32_t accepted = storage::open(mode, name, reading, request);
    if (accepted != 0) {
        op.state = static_cast<uint8_t>(1);
    } else {
        op.callback = 0;
        op.context = 0;
        operator_delete(request);
    }
    return accepted;
}

// 0x18017f6c — completion of a whole-file transfer: read the results, free the request, and
// finish the operation with result -1.
void f_18017f6c(Cpu& cpu) {
    const uint32_t request = cpu.r[0];
    const uint32_t status = as_request(request).status;
    const uint32_t size = as_request(request).size_result;
    const uint32_t op = as_request(request).file_object;
    {
        if (request != 0) {
            operator_delete(request);
        }
    }
    operation_finish(as_operation(op), status, size, 0xffff'ffffu, false);
}

// 0x18017f00 — completion of an open: store the file handle in the operation, free the
// request, and finish with the state set from the status.
void f_18017f00(Cpu& cpu) {
    const uint32_t request = cpu.r[0];
    const uint32_t status = as_request(request).status;
    const uint32_t size = as_request(request).size_result;
    Operation& op = as_operation(as_request(request).file_object);
    op.file_handle = as_request(request).file_handle;
    {
        if (request != 0) {
            operator_delete(request);
        }
    }
    operation_finish(op, status, size, 0, true);
}

// 0x180172e8 — the slot-level completion of a whole-file transfer: (service, op, status, size;
// stack: slot). Records the result and status, marks the slot done, and calls the slot's own
// callback if it has one: (slot id, status, request copy, extra word).
void f_180172e8(Cpu& cpu) {
    const uint32_t op = cpu.r[1], status = cpu.r[2], size = cpu.r[3];
    const uint32_t slot = stack_argument(0);
    as_file_slot(slot).result = as_operation(op).result;
    as_file_slot(slot).status = size;
    as_file_slot(slot).done = static_cast<uint8_t>(1);
    const uint32_t callback = as_file_slot(slot).arguments[0];
    if (callback != 0) {
        cpu.r[0] = as_file_slot(slot).id;
        cpu.r[1] = as_file_slot(slot).status;
        cpu.r[2] = slot + file_slot::REQUEST;
        cpu.r[3] = as_file_slot(slot).arguments[1];
        call_indirect(callback);  // tail call
    }
    (void)status;
}

// 0x18016ee8 / 0x18016e98 — the callbacks the issuers register: look the service up and
// forward (op, status, size; stack: slot) to the slot-level completion.
void f_18016ee8(Cpu& cpu) {
    GuestScratch frame(4 * 6);
    const uint32_t op = cpu.r[0], status = cpu.r[1], size = cpu.r[2], slot = cpu.r[3];
    cpu.r[0] = file_service_get();
    guest<uint32_t>(frame.at(4 * 0)) = slot;
    call_entry(f_180172e8, {cpu.r[0], op, status, size});
}

void f_18016e98(Cpu& cpu) {
    GuestScratch frame(4 * 6);
    const uint32_t op = cpu.r[0], status = cpu.r[1], size = cpu.r[2], slot = cpu.r[3];
    cpu.r[0] = file_service_get();
    guest<uint32_t>(frame.at(4 * 0)) = slot;
    call_entry(f_18017130, {cpu.r[0], op, status, size});
}

// 0x18017130 — the slot-level completion of an open: (service, op, status, size; stack: slot).
// The open's result is the file's size. A slot that is not active just reports; an active one
// goes on to the transfer it was opened for — positioned, or the whole file — unless the file
// has nothing to give (empty, or the read would start past its end), in which case it is
// closed again.
void f_18017130(Cpu& cpu) {
    uint32_t callback = 0, slot = 0;
    {
        GuestScratch frame(4 * 4);
        slot = stack_argument(4);  // the fifth argument
        if (slot == 0) {
            assert_trap(0x18017140u);
        }
        const uint32_t size = as_operation(slot + file_slot::ASYNC_REQUEST).result;
        as_file_slot(slot).result = size;
        as_file_slot(slot).busy = static_cast<uint8_t>(1);
        if (static_cast<uint32_t>(as_file_slot(slot).active) == 0) {
            as_file_slot(slot).done = static_cast<uint8_t>(1);
            callback = as_file_slot(slot).arguments[0];
            if (callback == 0) {
                return;
            }
        } else {
            FileRequest& request = as_file_request(slot + file_slot::REQUEST);
            const uint32_t kind = static_cast<uint32_t>(request.kind);
            const uint32_t offset = request.offset;
            const bool whole_file = kind == 0 || kind == 1;
            if (static_cast<uint32_t>(as_file_slot(slot).byte_06) != 0 &&
                (size == 0 || (whole_file && offset >= size))) {
                as_file_slot(slot).status = 0;
                cpu.r[0] = operation_close(as_operation(slot + file_slot::ASYNC_REQUEST),
                                           OPEN_THEN_CLOSE_COMPLETION, slot);
                return;
            }
            const uint32_t transfer = slot + file_slot::TRANSFER_REQUEST;
            guest<uint32_t>(cpu.r[SP]) =
                slot;  // the set-ups take the slot as their context, on the stack
            if (offset != 0 || kind == 2) {
                async_request_setup_positioned(as_request(transfer), offset, kind,
                                               POSITIONED_READ_COMPLETION, stack_argument(1));
            } else if (static_cast<uint32_t>(as_file_slot(slot).byte_06) == 0) {
                async_request_setup_read(transfer, request.buffer, request.length,
                                         TRANSFER_COMPLETION, slot);
            } else {
                async_request_setup_write(transfer, request.buffer, request.length,
                                          TRANSFER_COMPLETION, slot);
            }
            async_request_issue(slot + file_slot::ASYNC_REQUEST, transfer);
            return;
        }
    }
    // The slot's own callback, as a tail call: (slot id, file size, request copy, extra word).
    cpu.r[0] = as_file_slot(slot).id;
    cpu.r[1] = as_file_slot(slot).result;
    cpu.r[2] = slot + file_slot::REQUEST;
    cpu.r[3] = as_file_slot(slot).arguments[1];
    call_indirect(callback);
}

void f_18018660(Cpu& /*cpu*/) {  // release a lock word: nothing, the lock is kept
}

void f_18018570(Cpu& /*cpu*/) {  // the terminate handler: nothing
}

// 0x18018574 — register a destructor to run at exit: a node of the address and its argument
// on the C runtime's list. Returns 1, or 0 without memory.
uint32_t at_exit_register(uint32_t function, uint32_t argument, uint32_t dso) {
    const uint32_t node = libc::heap_allocate(16);
    if (node == 0) {
        return 0;
    }
    AtExitNode& entry = guest<AtExitNode>(node);
    entry.function = function;
    entry.argument = argument;
    entry.dso = dso;
    entry.next = guest<uint32_t>(libc::RUNTIME_AT_EXIT_LIST);
    guest<uint32_t>(libc::RUNTIME_AT_EXIT_LIST) = node;
    return 1;
}

}  // namespace minigolf::game
