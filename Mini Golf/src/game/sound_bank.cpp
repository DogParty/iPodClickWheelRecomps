// The sound bank loader (state at 0x18041418): given a list of (slot, index, file name)
// entries, it loads each WAV in turn through the file service — the 44-byte header first,
// which configures a fresh framework sound, then the data straight into the sound's buffer —
// and calls back when the list is done. Each entry ends with a status in the results table:
// 0 loaded, 2 out of memory, 3 bad slot or index, 5 already present or the file failed.
#include "sound_bank.h"

#include "async_request.h"
#include "calling.h"
#include "files.h"
#include "framework/audio.h"
#include "game_state.h"
#include "libc.h"
#include "records.h"
#include "resources.h"
#include "runtime/cpu.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "shims.h"
#include "sounds.h"
#include "state.h"

namespace minigolf::game {

namespace {

constexpr uint32_t BANK = SOUND_BANK;

constexpr uint32_t WAV_HEADER_SIZE = sizeof(WavHeader);

constexpr uint32_t STATUS_DONE = 0, STATUS_NO_MEMORY = 2, STATUS_BAD_ENTRY = 3, STATUS_FAILED = 5;
constexpr uint32_t NO_SOUND = 0xffff'ffffu, SLOT_INDEX_LIMIT = 0x40;
constexpr uint32_t HEADER_COMPLETION = 0x1801'7d14, DATA_COMPLETION = 0x1801'7a98;

// What a course loads with its sounds: its strings block, its sprite sheets, and the tables
// in the image that say which resources those are.
constexpr uint32_t COURSE_STRINGS = 0x180c'6d78, COURSE_STRINGS_SIZE = 0x2878;
constexpr uint32_t COURSE_PACK_TABLE = 0x1801'9010,
                   COURSE_PACK_ENTRY_SIZE = 0x68;  // +4: the strings resource
constexpr uint32_t COURSE_SHEET_TABLE =
    0x1801'8ff0;  // per course: sheet count, first sheet resource
constexpr uint32_t FIRST_SHEET_TEXTURE = 6;
constexpr uint32_t COURSE_SOUND_COUNT = 10,
                   COURSE_SOUND_FORMAT = 0x1800'47b0;     // "c%02dbank/%01d.wav"
constexpr uint32_t SOUND_FLAGS_COMPLETION = 0x1800'e644;  // slot_flags_from_table

SoundEntry& current_entry(SoundBank& bank) {
    return table_entry<SoundEntry>(bank.entries, bank.cursor);
}
SoundResult& current_result(SoundBank& bank) {
    return table_entry<SoundResult>(bank.results, bank.cursor);
}
void result_set(SoundBank& bank, uint32_t status) {
    current_result(bank).status = static_cast<uint8_t>(status);
}

// A read of `length` bytes at `offset` of the entry's file into `buffer`, with the given
// completion; the handle kept in the bank, -1 when the service refused.
uint32_t read_issue(SoundBank& bank, uint32_t buffer, uint32_t offset, uint32_t length,
                    uint32_t completion) {
    bank.request.buffer = buffer;
    bank.request.kind = static_cast<uint8_t>(0);
    bank.request.offset = offset;
    bank.request.length = length;
    bank.handle =
        file_begin(as_file_service(file_service_get()),
                   field_address(bank, offsetof(SoundBank, request)), completion, 0, false);
    return bank.handle;
}

// Close the file a completion was called for; the service's callbacks end this way.
void file_finish(uint32_t handle) {
    const uint32_t service = file_service_get();
    file_close(as_file_service(service), handle);
}

}  // namespace

// 0x18017930 — the list is done: the caller hears the worst status (5 if any entry failed)
// with the results table, and the bank is cleared for the next list. A callback that starts
// another load keeps the results it was handed.
void sound_bank_finish(SoundBank& bank) {
    uint32_t status = STATUS_DONE;
    for (uint32_t i = 0; i < bank.count; ++i) {
        if (table_entry<SoundResult>(bank.results, i).status != 0) {
            status = STATUS_FAILED;
        }
    }
    operator_delete(bank.entries);
    bank.entries = 0;
    bank.count = 0;
    bank.cursor = 0;
    bank.busy = static_cast<uint8_t>(0);
    bank.cancelled = static_cast<uint8_t>(0);
    const uint32_t callback = bank.callback;
    if (callback != 0) {
        call_indirect(callback, {status, bank.results, bank.context});
        if (static_cast<uint32_t>(bank.busy) != 0) {
            return;
        }
    }
    operator_delete(bank.results);
    bank.results = 0;
    bank.callback = 0;
    bank.context = 0;
}

// 0x18017ac0 — move on to the next entry that needs loading, from the cursor: entries with
// a bad slot or index, or already loaded, are marked and skipped; the first loadable one
// gets a fresh sound and its header read. Returns 1 while an entry is in flight, 0 at the end.
uint32_t sound_bank_advance(SoundBank& bank) {
    do {
        const uint32_t slot = current_entry(bank).slot;
        const uint32_t index = current_entry(bank).index;
        SoundResult& at = current_result(bank);
        if (slot >= SLOT_COUNT) {
            result_set(bank, STATUS_BAD_ENTRY);
        }
        if (index >= SLOT_INDEX_LIMIT) {
            result_set(bank, STATUS_BAD_ENTRY);
        } else if (at.status == 0) {
            if (sound_slot_present(slot, index) != 0) {
                result_set(bank, STATUS_FAILED);
            } else {
                const uint32_t sound = sound_slot_create(slot, index);
                at.sound = sound;
                if (static_cast<int32_t>(sound) < 0) {
                    result_set(bank, STATUS_FAILED);
                } else {
                    bank.request.mode = 0;
                    libc::string_copy_bounded(
                        field_address(bank, offsetof(SoundBank, request.name)),
                        field_address(current_entry(bank), offsetof(SoundEntry, name)), 0x100);
                    if (read_issue(bank, field_address(bank, offsetof(SoundBank, header)), 0,
                                   WAV_HEADER_SIZE, HEADER_COMPLETION) == NO_SOUND) {
                        at.sound = STATUS_FAILED;
                    }
                    if (at.status == 0) {
                        break;
                    }
                }
            }
        }
        // The entry is not loading: give back the sound it may have been given, and step on.
        if (static_cast<int32_t>(at.sound) >= 0) {
            sound_slot_release(slot, index);
            at.sound = NO_SOUND;
        }
        if (at.status == 0) {
            break;
        }
        bank.cursor = bank.cursor + 1;
    } while (static_cast<int32_t>(bank.cursor) < static_cast<int32_t>(bank.count));
    return static_cast<int32_t>(bank.cursor) < static_cast<int32_t>(bank.count) ? 1 : 0;
}

// 0x180179c8 — load a list of `count` entries; `callback(status, results, context)` when done.
// Returns 0 when the load started, 5 when the bank is busy or nothing needed loading, 2
// without memory for the results.
uint32_t sound_bank_load(SoundBank& bank, uint32_t entries, uint32_t count, uint32_t callback,
                         uint32_t context) {
    if (static_cast<uint32_t>(bank.busy) != 0) {
        return STATUS_FAILED;
    }
    if (bank.results != 0) {
        operator_delete(bank.results);
        bank.results = 0;
    }
    const uint32_t results = operator_new(count * sizeof(SoundResult));
    bank.results = results;
    if (results == 0) {
        return STATUS_NO_MEMORY;
    }
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(count); ++i) {
        table_entry<SoundResult>(results, i).status = 0;
        table_entry<SoundResult>(results, i).sound = NO_SOUND;
    }
    const uint32_t bytes = sizeof(SoundEntry) * count;
    bank.entries = operator_new(bytes);
    libc::memory_copy(bank.entries, entries, bytes);
    bank.count = count;
    bank.cursor = 0;
    bank.callback = callback;
    bank.context = context;
    const uint32_t busy = sound_bank_advance(bank);
    bank.busy = static_cast<uint8_t>(busy);
    bank.cancelled = static_cast<uint8_t>(0);
    return busy != 0 ? STATUS_DONE : STATUS_FAILED;
}

// 0x18017d3c — the header arrived (or not): the sound takes the WAV's format and a buffer
// for its data, which is read next; anything short of that fails the entry and moves on.
void sound_bank_header_read(SoundBank& bank, uint32_t handle, uint32_t status) {
    const uint32_t slot = current_entry(bank).slot;
    const uint32_t index = current_entry(bank).index;
    const uint32_t sound = current_result(bank).sound;
    const WavHeader& header = bank.header;
    bool next = true;
    if (status == bank.request.length) {
        audio::set_sound_sample_rate(sound, header.sample_rate);
        audio::set_sound_bits(sound, header.bits_per_sample);
        audio::set_sound_channels(sound, header.channels);
        audio::set_sound_data_size(sound, header.data_size);
        audio::set_sound_reserved(sound, 0);
        audio::set_sound_state(sound, 0);
        audio::set_sound_ready(sound, 1);
        const uint32_t buffer = libc::heap_allocate(header.data_size);
        uint32_t failure = STATUS_NO_MEMORY;
        if (buffer != 0) {
            audio::set_sound_buffer(sound, buffer);
            const uint32_t data = audio::sound_data(sound);
            if (read_issue(bank, data, WAV_HEADER_SIZE, header.data_size, DATA_COMPLETION) !=
                NO_SOUND) {
                next = false;
                failure = STATUS_DONE;
            } else {
                failure = STATUS_FAILED;
            }
        }
        if (failure != STATUS_DONE) {
            sound_slot_release(slot, index);
            result_set(bank, failure);
            current_result(bank).sound = NO_SOUND;
        }
    }
    if (next) {
        bank.cursor = bank.cursor + 1;
        if (static_cast<int32_t>(bank.cursor) >= static_cast<int32_t>(bank.count) ||
            sound_bank_advance(bank) == 0) {
            sound_bank_finish(bank);
        }
    }
    file_finish(handle);
}

// 0x18017c70 — the data arrived (or not): a short read fails the entry; then the next entry,
// unless the load was cancelled, and the list finishes when none is left.
void sound_bank_data_read(SoundBank& bank, uint32_t handle, uint32_t status) {
    if (status != bank.request.length) {
        sound_slot_release(current_entry(bank).slot, current_entry(bank).index);
        result_set(bank, STATUS_FAILED);
        current_result(bank).sound = NO_SOUND;
    }
    bank.cursor = bank.cursor + 1;
    const bool more = static_cast<int32_t>(bank.cursor) < static_cast<int32_t>(bank.count) &&
                      static_cast<uint32_t>(bank.cancelled) == 0;
    if (!more || sound_bank_advance(bank) == 0) {
        sound_bank_finish(bank);
    }
    file_finish(handle);
}

// 0x18004660 — the resources a course's play needs beyond its holes: its strings, its
// sprite-sheet images (textures 6 onwards), and its ten sounds, "c%02dbank/%01d.wav", loaded
// into the settings' sound slot with slot_flags_from_table as the completion.
void course_sounds_load() {
    GuestScratch frame(4 * 6 + 0xa60);
    if (static_cast<int32_t>(game_state_block().word_28) <= 0) {
        assert_trap(0x18004678u);
    }
    libc::memory_clear(COURSE_STRINGS, COURSE_STRINGS_SIZE);
    const uint32_t course = static_cast<uint32_t>(static_cast<uint32_t>(menu_state().course));
    resource_load(as_pack(game_state_block().pack_course[course]),
                  guest_array<uint32_t>(COURSE_PACK_TABLE + course * COURSE_PACK_ENTRY_SIZE)[1],
                  COURSE_STRINGS, COURSE_STRINGS_SIZE);
    const uint32_t* sheets = guest_array<uint32_t>(COURSE_SHEET_TABLE + course * 8);
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(sheets[0]); ++i) {
        image_from_resource(
            as_image(TITLE_IMAGE + (FIRST_SHEET_TEXTURE + i) * TEXTURE_TABLE_STRIDE), 1,
            as_pack(game_state_block().pack_sheets[course]), sheets[1] + i,
            FIRST_SHEET_TEXTURE + i);
    }
    files_release(0);
    const uint32_t entries = frame.at(0x10);
    for (uint32_t i = 0; i < COURSE_SOUND_COUNT; ++i) {
        const uint32_t at = entries + i * static_cast<uint32_t>(sizeof(SoundEntry));
        guest<SoundEntry>(at).slot = play_state().slot;
        guest<SoundEntry>(at).index = i;
        libc::format_text(at + static_cast<uint32_t>(offsetof(SoundEntry, name)),
                          COURSE_SOUND_FORMAT, {course, i});
    }
    stack_arguments({COURSE_SOUND_COUNT});
    sound_bank_load(as_bank(BANK), entries, COURSE_SOUND_COUNT, SOUND_FLAGS_COMPLETION,
                    COURSE_SOUND_COUNT);
}

// 0x18017eb8 (0x180186e8 for the one bank) — the bank as built: idle and empty.
void sound_bank_construct(uint32_t bank) {
    as_bank(bank).busy = static_cast<uint8_t>(0);
    as_bank(bank).cancelled = static_cast<uint8_t>(0);
    as_bank(bank).entries = 0;
    as_bank(bank).count = 0;
    as_bank(bank).cursor = 0;
    as_bank(bank).handle = 0xffff'ffffu;
    FileRequest& request = as_bank(bank).request;
    request.mode = 0;
    request.buffer = 0;
    request.kind = 0;
    request.offset = 0;
    request.length = 0;
    request.name[0] = 0;
    as_bank(bank).callback = 0;
    as_bank(bank).context = 0;
    as_bank(bank).results = 0;
}

// --- shims -----------------------------------------------------------------------------------

// The service callbacks, (handle, status, request, context), for the one bank.
void f_18017d14(Cpu& cpu) {
    const uint32_t handle = cpu.r[0], status = cpu.r[1];
    sound_bank_header_read(as_bank(BANK), handle, status);
}

void f_18017a98(Cpu& cpu) {
    const uint32_t handle = cpu.r[0], status = cpu.r[1];
    sound_bank_data_read(as_bank(BANK), handle, status);
}

void f_180186e8(Cpu& /*cpu*/) {
    sound_bank_construct(BANK);
}

}  // namespace minigolf::game
