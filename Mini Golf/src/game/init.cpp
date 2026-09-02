// Start-up of the game proper: everything the first tick does before the title screen appears.
//
// This is where most of the big game-state object gets its initial values, so it is also where
// many of its fields first got names (game_state.h). Fields whose purpose is not yet understood
// are named by their offset and marked "(inferred)"; they will be renamed as the code that uses
// them is decompiled.
#include "init.h"

#include "calling.h"
#include "framework/audio.h"
#include "framework/device.h"
#include "game_state.h"
#include "libc.h"
#include "records.h"
#include "resources.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "save_files.h"
#include "screens.h"
#include "sounds.h"
#include "state.h"

namespace minigolf::game {

namespace {

// Still recompiled, named by their use here (inferred).

constexpr uint32_t BSS_HEAD_SIZE = 0x1020;
constexpr uint32_t BSS_HEAD_WORD = 0x1801'a50c;  // cleared with the BSS head (inferred)
constexpr uint32_t SCORE_ROWS = 16, SCORE_CLEARED = 10;
constexpr uint32_t TITLE_TEXTURE_TARGET = 0x1801'be6c;
constexpr uint32_t RECORD_B = 0xe50;  // offset of record B inside RECORD_B_BLOCK

// The font (text block index) used for each language Settings #0 can report.
uint32_t font_for_language(int32_t language) {
    switch (language) {
    case 1:
        return 1;
    case 2:
    case 3:
        return 3;
    case 4:
        return 4;
    case 5:
        return 5;
    case 6:
    case 7:
    case 8:
        return 8;
    case 9:
        return 9;
    case 10:
        return 10;
    case 12:
    case 13:
        return 13;
    case 14:
        return 14;
    case 16:
        return 16;
    default:
        return 0;
    }
}

// 0x18007510 — clear the first 0x1020 bytes of the BSS and one word beside it.
void reset_bss_head() {
    libc::memory_clear(BSS_START, BSS_HEAD_SIZE);  // memclr
    guest<uint32_t>(BSS_HEAD_WORD) = 0;
}

// 0x1800cd10 — clear two word columns of every entry in the score table.
void score_table_clear() {
    for (uint32_t row = 0; row < SCORE_ROWS; ++row) {
        FileEntry& entry = table_entry<FileEntry>(SCORE_TABLE, row);
        for (uint32_t i = 0; i < SCORE_CLEARED; ++i) {
            entry.chunks[i] = 0;
            entry.sizes[i] = 0;
        }
    }
}

}  // namespace

// 0x18006ecc — load the title image once, then reset two records from their defaults.
void load_title_and_defaults() {
    GuestScratch frame(4 * 6);
    if (static_cast<uint32_t>(app2_state().title_loaded) == 0) {
        texture_from_pixels(as_image(TITLE_IMAGE), 1, TITLE_IMAGE_WIDTH, TITLE_IMAGE_HEIGHT,
                            TITLE_TEXTURE_TARGET, 0);
        app2_state().title_loaded = static_cast<uint8_t>(1);
    }
    libc::memory_copy(RECORD_A, DEFAULT_RECORD_A, RECORD_SIZE);  // memcpy
    // Two image records in the block (at +0xe0c and +0xe48; the defaults are their nine header
    // words): width and height from the header's cell size, variant 1, texture index cleared.
    ImageRecord& first = as_image(RECORD_B_BLOCK + 0xe0c);
    first.width = first.cell_u;
    first.height = first.cell_height;
    first.variant = 1;
    first.texture_index = 0;
    libc::memory_copy(RECORD_B_BLOCK + RECORD_B, DEFAULT_RECORD_B, RECORD_SIZE);
    ImageRecord& second = as_image(RECORD_B_BLOCK + 0xe48);
    second.width = second.cell_u;
    second.height = second.cell_height;
    second.variant = 1;
    second.texture_index = 0;
}

namespace {

// 0x18014ce0 — set option bits on the object APP2_FLAGS_POINTER points to.
void option_bits_set(uint32_t bits) {
    flags_object().option_bits = flags_object().option_bits | bits;
}

}  // namespace

void slots_reset() {
    SoundSlots& slots = sound_slots();
    for (uint32_t slot = 0; slot < SoundSlots::COUNT; ++slot) {
        slots.enabled[slot] = 0;
        for (uint32_t i = 0; i < SoundSlots::INDEX_LIMIT; ++i) {
            slots.handle[slot][i] = 0xffff'ffffu;
        }
    }
}

int32_t slot_allocate() {
    SoundSlots& slots = sound_slots();
    for (uint32_t slot = 0; slot < SoundSlots::COUNT; ++slot) {
        if (slots.enabled[slot] == 0) {
            slots.enabled[slot] = 1;
            return static_cast<int32_t>(slot);
        }
    }
    return -1;
}

int32_t settings_language() {
    GuestScratch frame(4 * 4);  // slot 0: the size in/out, slot 1: the value out
    guest<uint32_t>(frame.at(4 * 0)) = 4;
    const int32_t status =
        static_cast<int32_t>(device::setting(LANGUAGE_KEY, frame.at(4 * 1), frame.at(4 * 0)));
    return status >= 0 ? static_cast<int32_t>(guest<uint32_t>(frame.at(4 * 1))) : status;
}

void game_init() {
    libc::string_copy(VERSION_STRING, VERSION_LITERAL);  // strcpy "1.0.0"
    app2_state().exiting = static_cast<uint8_t>(0);
    reset_bss_head();
    libc::memory_clear(GAME_STATE, GAME_STATE_SIZE);  // memclr
    current_screen() = Screen{};  // the screen's functions lived in the block that was cleared
    score_table_clear();
    slots_reset();

    play_state().device_level = device::brightness() / 10 * 10;
    const uint32_t volume_scale = audio::music_level_scale();
    play_state().music_level = audio::music_level() * 100 / volume_scale;
    play_state().audio_flag = static_cast<uint8_t>((audio::engine_ready() & 0xff) == 1 ? 1 : 0);
    option_bits_set(0xe);
    play_state().wheel_direction = static_cast<uint8_t>(7);
    play_state().wheel_repeat_limit = 0x14;
    play_state().word_808 = 4;
    for (uint32_t course = 0; course < game_state::COURSE_RECORD_COUNT; ++course) {
        game_state_block().course_table[(course * game_state::COURSE_RECORD_SIZE + 0xa10) / 4] =
            0xffff'ffffu;
    }
    play_state().slot = static_cast<uint32_t>(slot_allocate());
    for (uint32_t i = 0; i < 10; ++i) {
        play_state().sound_enabled[i] = 0;
    }
    play_state().word_7e8 = 0;

    game_state_block().handle = renderer_create();
    if (game_state_block().handle == 0) {
        assert_trap(0x18012374u);
    }
    load_title_and_defaults();
    const int32_t language = static_cast<int32_t>(static_cast<int8_t>(settings_language()));
    text_block().byte_750 = static_cast<uint8_t>(font_for_language(language));

    game_state_block().word_28 = 0x82b80;
    play_state().byte_7be = static_cast<uint8_t>(1);
    play_state().byte_819 = static_cast<uint8_t>(1);
    play_state().byte_81a = static_cast<uint8_t>(0);
    play_state().ticked = static_cast<uint8_t>(1);
    app2_state().phase = static_cast<uint8_t>(1);
}

}  // namespace minigolf::game
