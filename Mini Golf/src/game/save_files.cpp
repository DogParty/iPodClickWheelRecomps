// The save and statistics files as the game's flow sees them: a ring of eight records
// (0x18037a1c), each a file object and the name it was opened with, and the chunked transfer
// of a score file through the file table (FILE_TABLE) that the start-up state machine drives
// one entry at a time.
#include "calling.h"
#include "file_objects.h"
#include "game_state.h"
#include "libc.h"
#include "records.h"
#include "resources.h"
#include "runtime/cpu.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "state.h"

namespace minigolf::game {

namespace {

constexpr uint32_t FILE_RECORDS = 0x1803'7a1c, FILE_RECORD_COUNT = 8, FILE_RECORD_SIZE = 0x204;
constexpr uint32_t FILE_RECORD_NEXT = 0x1801'a50c;  // where the search for a free record starts
constexpr uint32_t MODE_READ = 0x1800'7618, MODE_WRITE = 0x1800'761c;  // "rb", "wb"
constexpr uint32_t NAME_LIMIT = 0x200;

// The score files: sixteen descriptors (name, size) per group, by the file table's (group,
// index) pair; a file is read in chunks of at most CHUNK_BYTES.
constexpr uint32_t CHUNK_BYTES = 0x7e7ec;
constexpr uint32_t TITLE_PACK_ENTRY = 5;  // the entry that opens the title pack first

}  // namespace

// 0x1800753c — a record for `name`, opened for reading or (`write`) writing; 0 when the
// framework had no file object for it. The search for a free record goes round the ring
// from where the last one was found, and does not end until it finds one.
uint32_t file_record_open(uint32_t name, uint32_t write) {
    if (name == 0) {
        assert_trap(0x1800754cu);
    }
    if (libc::string_length(name) >= NAME_LIMIT) {
        assert_trap(0x18007560u);
    }
    if (write >= 2) {
        assert_trap(0x1800756cu);
    }
    if (guest<uint32_t>(FILE_RECORD_NEXT) >= FILE_RECORD_COUNT) {
        assert_trap(0x18007588u);
    }
    uint32_t record = 0;
    do {
        const uint32_t index = guest<uint32_t>(FILE_RECORD_NEXT);
        record = FILE_RECORDS + index * FILE_RECORD_SIZE;
        guest<uint32_t>(FILE_RECORD_NEXT) = index + 1 == FILE_RECORD_COUNT ? 0 : index + 1;
    } while (as_file_record(record).object != 0);
    const uint32_t object = file_object_acquire(name, write != 0 ? MODE_WRITE : MODE_READ);
    if (object == 0) {
        libc::memory_clear(address_of(record), FILE_RECORD_SIZE);
        return 0;
    }
    as_file_record(record).object = object;
    libc::string_copy(record + static_cast<uint32_t>(offsetof(FileRecord, name)), name);
    return record;
}

// 0x18007620 — transfer `bytes` from `buffer` through a record; the count, or 0 if the
// file object would not take it.
uint32_t file_record_transfer(FileRecord& record, uint32_t buffer, uint32_t bytes) {
    if (record.object == 0) {
        assert_trap(0x18007640u);
    }
    if (buffer == 0) {
        assert_trap(0x1800764cu);
    }
    if (bytes == 0) {
        assert_trap(0x18007658u);
    }
    return file_object_write(as_file_object(record.object), buffer, 1, bytes) != 0 ? bytes : 0;
}

// 0x180074d4 — close a record's file and clear the record.
void file_record_close(FileRecord& record) {
    {
        if (record.object == 0) {
            assert_trap(0x180074f0u);
        }
        file_object_close(as_file_object(record.object));
    }
    libc::memory_clear(address_of(record), FILE_RECORD_SIZE);
}

// 0x18010860 — begin loading the score file the start-up state machine is at (app2::
// SCORE_ENTRY): its descriptor names the file and its size, which is split into chunks the
// tracked allocator provides; the title pack is opened on the way past entry 5; then the
// file is opened for reading and its first chunk requested.
uint32_t score_file_begin(uint32_t state) {
    uint32_t record = 0, chunk = 0, size = 0;
    {
        if (state == 0) {
            assert_trap(0x1801086cu);
        }
        const uint32_t entry = app2_state().score_entry;
        if (static_cast<int32_t>(entry) >= static_cast<int32_t>(FILE_COUNT)) {
            assert_trap(0x18010880u);
        }
        FileEntry& file = as_file(FILE_TABLE + entry * FILE_SIZE);
        const uint32_t descriptor = FILE_KINDS + file.text * FILE_KIND_ROW +
                                    file.entry * static_cast<uint32_t>(sizeof(FileKind));
        for (uint32_t i = 0; i < file::CHUNK_LIMIT; ++i) {
            file.chunks[i] = 0;
            file.sizes[i] = 0;
        }
        uint32_t remaining = as_file_kind(descriptor).size, chunks = 0;
        game_state_block().loaded[game_state::LOADED_ENTRY_COUNT] = static_cast<int8_t>(0);
        while (remaining != 0) {
            const uint32_t bytes = remaining > CHUNK_BYTES ? CHUNK_BYTES : remaining;
            const uint32_t memory = tracked_allocate(bytes);
            file.chunks[chunks] = memory;
            if (memory == 0) {
                assert_trap(remaining > CHUNK_BYTES ? 0x1801091cu : 0x18010938u);
            }
            file.sizes[chunks] = bytes;
            remaining -= bytes;
            ++chunks;
            game_state_block().loaded[game_state::LOADED_ENTRY_COUNT] = static_cast<int8_t>(
                static_cast<uint8_t>(game_state_block().loaded[game_state::LOADED_ENTRY_COUNT]) +
                1);
        }
        file.name = descriptor;
        file.cursor = file.chunks[0];
        file.offset = 0;
        file.chunk = 0;
        app2_state().file_status = 0;
        play_state().word_7c4 = 0;
        game_state_block().loaded[game_state::LOADED_ENTRY] = static_cast<int8_t>(0);
        if (app2_state().score_entry == TITLE_PACK_ENTRY) {
            title_pack_open();
        }
        record = file_record_open(as_file_kind(descriptor).name, 0);
        play_state().pointer_7d0 = record;
        const uint32_t current =
            static_cast<uint32_t>(game_state_block().loaded[game_state::LOADED_ENTRY]);
        chunk = file.chunks[current];
        size = file.sizes[current];
    }
    return file_record_transfer(as_file_record(record), chunk, size);
}

}  // namespace minigolf::game
