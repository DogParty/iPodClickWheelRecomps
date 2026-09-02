// The game's file objects: ten small records (0x1803fc34, 0x1c bytes each) that wrap a
// file-service slot for the save and statistics code. An object is opened by area and name,
// written to or read from once its open completes, and closed; every step is asynchronous and
// lands in file_object_completed through the service's callbacks.
#include "file_objects.h"

#include "async_request.h"
#include "calling.h"
#include "files.h"
#include "libc.h"
#include "records.h"
#include "resources.h"
#include "runtime/cpu.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "shims.h"
#include "state.h"

namespace minigolf::game {

namespace {

constexpr uint32_t FILE_OBJECTS = 0x1803'fc34, FILE_OBJECT_SIZE = 0x1c, FILE_OBJECT_COUNT = 10;
constexpr uint32_t REQUEST_RECORDS = 0x1803'fd8c, REQUEST_RECORD_SIZE = 0x58,
                   REQUEST_RECORD_COUNT = 16;
constexpr uint32_t REQUEST_RECORDS_DESTRUCTOR = 0x1800'24a0;

// The files an object can name, each in its own framework area (0, 1, 2).
constexpr uint32_t FILE_GAMES_RO = 0x1801'6d80, FILE_GAMEDATA_RW = 0x1801'6d8c,
                   FILE_GAMESTATS_WO = 0x1801'6d98;

constexpr uint32_t COMPLETION = 0x1801'6ca0;  // the service callback (below)

// 0x1800142c — the C runtime's string compare, as the game uses it: non-zero when equal.

}  // namespace

// 0x18016df0 — an object as built: closed and idle.
constexpr uint32_t NO_HANDLE = 0xffff'ffffu;  // a closed object's service handle

void file_object_construct(FileObject& object) {
    object = FileObject{};
    object.handle = NO_HANDLE;
}

// 0x18016b84 — an object nobody is using.
uint32_t file_object_free(FileObject& object) {
    return static_cast<uint32_t>(object.state) == 0 && static_cast<uint32_t>(object.open) == 0 ? 1
                                                                                               : 0;
}

// 0x18016abc — the status of a finished write, if there is one: 1 and the status through
// `out`, else 0.
uint32_t file_object_written(FileObject& object, uint32_t out) {
    if (static_cast<uint32_t>(object.open) == 0 || static_cast<uint32_t>(object.written) == 0) {
        return 0;
    }
    guest<uint32_t>(out) = object.status;
    return 1;
}

// 0x18016cb8 — open the file `name` ("games_RO", "gamedata_RW" or "gamestats_WO" — the save
// and statistics files; anything else opens like the first) with a C-style `mode`, "rb" or
// "wb": a mode starting with 'w' opens for writing.
void file_object_open(FileObject& object, uint32_t name, uint32_t mode) {
    uint32_t area = 0;
    if (libc::strings_equal(name, FILE_GAMES_RO) == 0) {
        if (libc::strings_equal(name, FILE_GAMEDATA_RW) != 0) {
            area = 1;
        } else if (libc::strings_equal(name, FILE_GAMESTATS_WO) != 0) {
            area = 2;
        }
    }
    // The original looks at the mode's first character once per character of the mode.
    const uint32_t reading = guest<uint8_t>(mode) != 0 && guest<uint8_t>(mode) == 'w' ? 0 : 1;
    const uint32_t service = file_service_get();
    object.handle = file_open_positioned(as_file_service(service), area, name, reading, COMPLETION,
                                         address_of(object));
    object.open = 0;
    object.written = 0;
    object.write_pending = 0;
    object.read_done = 0;
    object.read_pending = 0;
    object.status = 0;
    object.state = static_cast<uint8_t>(1);
}

// 0x18016ae4 (0x18016e20 with the object last) — write `count` items of `size` from
// `buffer`. An object still opening remembers the request for its open's completion (1); an
// object never opened ignores it (0). Otherwise, what the service answered.
uint32_t file_object_write(FileObject& object, uint32_t buffer, uint32_t count, uint32_t size) {
    if (static_cast<uint32_t>(object.open) != 0) {
        if (static_cast<uint32_t>(object.state) != 0) {
            assert_trap(0x18016b10u);
        }
        const uint32_t service = file_service_get();
        const uint32_t accepted =
            file_write_begin(as_file_service(service), object.handle, buffer, size * count);
        object.state = static_cast<uint8_t>(3);
        object.written = 0;
        object.write_pending = 0;
        object.read_done = 0;
        object.read_pending = 0;
        return accepted;
    }
    if (object.handle == 0xffff'ffffu) {
        return 0;
    }
    if (static_cast<uint32_t>(object.state) != 1) {
        assert_trap(0x18016b5cu);
    }
    object.written = static_cast<uint8_t>(0);
    object.write_pending = static_cast<uint8_t>(1);
    object.read_done = static_cast<uint8_t>(0);
    object.read_pending = static_cast<uint8_t>(0);
    object.buffer = buffer;
    object.count = count;
    object.size = size;
    return 1;
}

// 0x18016ba0 (0x18016ca0 as the service's callback) — a step of an object's file finished:
// an open goes on to the write or read that was waiting for it; a close forgets the handle;
// a transfer leaves the object idle with its flag set.
void file_object_completed(FileObject& object, uint32_t handle, uint32_t status) {
    switch (static_cast<uint32_t>(object.state)) {
    case 1: {
        object.open = static_cast<uint8_t>(1);
        const uint32_t pending_write = static_cast<uint32_t>(object.write_pending);
        if (pending_write != 0) {
            if (status == 0) {
                object.state = static_cast<uint8_t>(0);
                object.write_pending = static_cast<uint8_t>(0);
                object.written = static_cast<uint8_t>(1);
                break;
            }
            const uint32_t service = file_service_get();
            file_write_begin(as_file_service(service), object.handle, object.buffer,
                             object.size * object.count);
            object.write_pending = static_cast<uint8_t>(0);
            object.state = static_cast<uint8_t>(3);
        } else if (static_cast<uint32_t>(object.read_pending) != 0) {
            const uint32_t service = file_service_get();
            file_read_begin(as_file_service(service), object.handle, object.buffer,
                            object.size * object.count);
            object.read_pending = static_cast<uint8_t>(0);
            object.state = static_cast<uint8_t>(4);
        }
        break;
    }
    case 2: {
        object.handle = 0xffff'ffffu;
        object.state = static_cast<uint8_t>(0);
        object.open = static_cast<uint8_t>(0);
        const uint32_t service = file_service_get();
        file_close(as_file_service(service), handle);
        break;
    }
    case 3:
        object.state = static_cast<uint8_t>(0);
        object.written = static_cast<uint8_t>(1);
        break;
    case 4:
        object.state = static_cast<uint8_t>(0);
        object.read_done = static_cast<uint8_t>(1);
        break;
    default:
        object.state = static_cast<uint8_t>(0);
        break;
    }
    object.status = status;
}

// 0x18016dac — close an idle object's file; the object is closing until the completion.
void file_object_close(FileObject& object) {
    if (static_cast<uint32_t>(object.state) != 0) {
        assert_trap(0x18016dc0u);
    }
    const uint32_t handle = object.handle;
    const uint32_t service = file_service_get();
    file_close_begin(as_file_service(service), handle);
    object.state = static_cast<uint8_t>(2);
}

// 0x18016e38 — open `name` with `mode` on the first free object. Returns the object, or the
// address past the last one when all ten are in use.
uint32_t file_object_acquire(uint32_t name, uint32_t mode) {
    uint32_t object = FILE_OBJECTS;
    for (uint32_t i = 0; i < FILE_OBJECT_COUNT; ++i, object += FILE_OBJECT_SIZE) {
        if (file_object_free(as_file_object(object)) != 0) {
            file_object_open(as_file_object(object), name, mode);
            break;
        }
    }
    return object;
}

// 0x180186cc — the ten file objects, constructed at start-up.
void file_objects_construct() {
    array_construct(
        FILE_OBJECTS, 0, FILE_OBJECT_SIZE, 0, FILE_OBJECT_COUNT, 0,
        [](uint32_t element) { file_object_construct(as_file_object(element)); }, 0);
}

// 0x180186f4 — the sixteen 0x58-byte request records at 0x1803fd8c, constructed, and their
// destructor (0x180024a0) registered to run at exit.
void request_records_construct() {
    array_construct(
        REQUEST_RECORDS, 0, REQUEST_RECORD_SIZE, 0, REQUEST_RECORD_COUNT, 0,
        [](uint32_t element) { async_request_construct(as_request(element)); }, 0);
    at_exit_register(0, REQUEST_RECORDS_DESTRUCTOR, 0x1801a500u);
}

// --- shims -----------------------------------------------------------------------------------

// The service callback: (handle, status, request, object)
void f_18016ca0(Cpu& cpu) {
    file_object_completed(as_file_object(cpu.r[3]), cpu.r[0], cpu.r[1]);
}

}  // namespace minigolf::game
