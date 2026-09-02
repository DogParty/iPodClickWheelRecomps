// The top-level flow of the game: save file in, resources in, save file out, then the screens.
//
// `flow_step` (0x1800ecd8) runs once per game step. A phase byte selects what it does; the
// phases form the boot sequence 1 → 2 → (3 → 4 →) 5 → 6 → 7 → 8 → 9 → 0 and then stay at 0,
// where the current screen (title, menus, name entry, the course itself) gets its tick. Phase
// numbers are the game's own; their names here are inferred from what each phase does.
//
// The return value is what the tick reports upward (app.cpp `game_step`): 2 normally, 5 when
// the player has been idle long enough to suspend, 1 at the earlier idle notice, 3 when the
// game is exiting.
//
// Long because it is one state machine, phase by phase, and the phases share the loading
// state they advance through.
#include "flow.h"

#include "calling.h"
#include "course.h"
#include "file_objects.h"
#include "files.h"
#include "fonts.h"
#include "framework/device.h"
#include "game_state.h"
#include "hole_load.h"
#include "hole_tick.h"
#include "input.h"
#include "libc.h"
#include "menu.h"
#include "name_entry.h"
#include "random.h"
#include "records.h"
#include "resources.h"
#include "runtime/cpu.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "save_files.h"
#include "screen_ticks.h"
#include "screens.h"
#include "shims.h"
#include "state.h"
#include "title.h"

namespace minigolf::game {

void music_start(uint32_t in_game);

void score_entries_begin();
void score_entry_request(uint32_t text, uint32_t entry);
void idle_suspend_notify(uint32_t answer);
void save_reset(uint32_t reset_options, uint32_t even_in_progress, uint32_t forget_screen);

namespace {

enum Phase : uint32_t {
    RUN_SCREEN = 0,
    LOAD_SAVE_START = 1,
    LOAD_SAVE_WAIT = 2,
    LOAD_BACKUP_START = 3,
    LOAD_BACKUP_WAIT = 4,
    RESET_SCORES = 5,
    LOAD_SCORE_ENTRIES = 6,
    LOAD_RESOURCES = 7,
    WRITE_SAVE_START = 8,
    WRITE_SAVE_WAIT = 9,
};

enum Result : uint32_t {
    RESULT_IDLE_NOTICE = 1,
    RESULT_NORMAL = 2,
    RESULT_EXITING = 3,
    RESULT_SUSPEND = 5
};

constexpr uint32_t SCREEN_COUNT = 14;
constexpr uint32_t SCORE_ENTRY_COUNT = 12;
constexpr uint32_t SCORE_TABLE_BYTES = 0x680;
constexpr uint32_t LANGUAGE_CODES[11] = {0x1800'cce4, 0x1800'cce8, 0x1800'ccec, 0x1800'ccf0,
                                         0x1800'ccf4, 0x1800'ccf8, 0x1800'ccfc, 0x1800'cd00,
                                         0x1800'cd04, 0x1800'cd08, 0x1800'cd0c};
constexpr uint32_t FILE_HANDLE_NONE = 0xffff'ffffu;
constexpr uint32_t RESOURCE_ID_COURSE_BLOCK = 0x140;

// Still recompiled, named by their use here (inferred). The file functions are the game's own
// wrapper around AsyncFileIO.

// Phase 0: the per-screen tick functions, by screen id; each takes the milliseconds and answers
// 1 to request a suspend. Screens 11 and 12 have their own entries below.
ScreenStep screen_step(uint32_t screen) {
    switch (screen) {
    case 0:
        return plain_screen_step;
    case 5:
        return course_select_tick;
    case 7:
        return page_tick;
    case 8:
        return dialog_tick;
    case 10:
        return name_entry_tick;
    default:
        return menu_screen_step;  // 1, 2, 3, 4, 6, 9, 12
    }
}

// Phase 7: what to run when a screen is entered, by screen id (0 and 11 handled separately).
ScreenEnter screen_enter(uint32_t screen) {
    switch (screen) {
    case 1:
        return main_menu_screen_enter;
    case 2:
        return game_modes_screen_enter;
    case 3:
        return options_screen_enter;
    case 4:
        return help_screen_enter;
    case 5:
        return course_select_screen_enter;
    case 6:
        return hole_select_screen_enter;
    case 7:
        return page_screen_enter;
    case 10:
        return name_entry_enter;
    default:
        return nullptr;  // 8, 9 and anything above 12 were asserts
    }
}

struct Flow {
    GuestScratch frame;
    uint32_t milliseconds;
    uint32_t result = RESULT_NORMAL;

    static constexpr uint32_t LOCALS = 0x130;
    static constexpr uint32_t REQUEST = 0x10;  // the file request object lives in the locals

    explicit Flow(uint32_t ms) : frame(4 * 10 + LOCALS), milliseconds(ms) {}

    uint32_t request() const { return frame.at(REQUEST); }

    static uint32_t settings() { return GAME_STATE + game_state::SETTINGS; }
    static uint32_t screen_id() {
        return static_cast<uint32_t>(static_cast<int8_t>(screen_block_byte(game_state::SCREEN_ID)));
    }
    static void set_phase(uint32_t phase) { app2_state().phase = static_cast<uint8_t>(phase); }

    // Begin reading or writing a save file into the save buffer. Returns the file's handle,
    // FILE_HANDLE_NONE when the service refused it.
    uint32_t start_file(uint32_t name, bool write) {
        file_request_prepare(as_file_request(request()), 1, name,
                             GAME_STATE + game_state::SAVE_BUFFER, 0, 0, game_state::SAVE_SIZE);
        const uint32_t handle =
            file_begin(as_file_service(file_service_get()), request(), 0, 0, write);
        play_state().file_handle = handle;
        return handle;
    }

    // Poll a file operation; when finished, record its status and close the handle.
    bool finish_file() {
        if (file_finished(as_file_service(file_service_get()), milliseconds) == 0) {
            return false;
        }
        const uint32_t handle = play_state().file_handle;
        app2_state().file_status = file_status(as_file_service(file_service_get()), handle);
        file_close(as_file_service(file_service_get()), handle);
        play_state().file_handle = FILE_HANDLE_NONE;
        return true;
    }

    // A complete save image with both magic words in place.
    static bool save_buffer_valid(uint32_t expected_size) {
        const uint32_t buffer = GAME_STATE + game_state::SAVE_BUFFER;
        return app2_state().file_status == expected_size && guest<uint32_t>(buffer) == SAVE_MAGIC &&
               guest<uint32_t>(buffer + game_state::SAVE_MAGIC_END) == SAVE_MAGIC;
    }

    void adopt_save_buffer() {
        libc::memory_copy(GAME_STATE + game_state::SAVE_DATA, GAME_STATE + game_state::SAVE_BUFFER,
                          game_state::SAVE_COPY_SIZE);
    }

    void copy_screen_byte_if_saved() {
        if (static_cast<uint32_t>(game_state_block().save_data_byte_5) != 0) {
            text_block().byte_745 =
                static_cast<uint8_t>(static_cast<uint32_t>(game_state_block().byte_82d9a));
        }
    }

    // --- phases ------------------------------------------------------------------------------

    void run_screen() {
        text_block().selection = static_cast<uint8_t>(7);
        input_gather();

        // The first active player slot, if any, becomes the selection; it also resets idling.
        static constexpr uint32_t slots[] = {0, 1, 2, 3, 5, 6};
        bool player_active = false;
        for (const uint32_t slot : slots) {
            if (wheel_slot_at(slot).flags & 4) {
                text_block().selection = static_cast<uint8_t>(slot);
                player_active = true;
                break;
            }
        }
        if (player_active) {
            if (text_block().idle_ms >= IDLE_NOTICE_MS) {
                device::set_idle_inhibited(0);  // 0x18014cfc: the idle reset
            }
            text_block().idle_ms = 0;
        }

        const uint32_t screen = screen_id();
        if (screen >= SCREEN_COUNT) {
            assert_trap(0x1800eef0u);
        }
        if (screen == 11) {
            hole_tick(milliseconds);  // the name-entry / menu screen
        } else if (screen == 13) {
            hole_start();
        } else {
            if (screen_step(screen)(milliseconds) == 1) {
                result = RESULT_SUSPEND;
                return;
            }
        }

        const uint32_t idle = text_block().idle_ms + milliseconds;
        text_block().idle_ms = idle;
        text_block().frame_count = game_state_block().text[467] + 1;
        if (static_cast<int32_t>(idle) >= static_cast<int32_t>(IDLE_SUSPEND_MS)) {
            result = RESULT_SUSPEND;
            idle_suspend_notify(idle);
        } else if (static_cast<int32_t>(idle) >= static_cast<int32_t>(IDLE_NOTICE_MS)) {
            result = RESULT_IDLE_NOTICE;
            device::set_idle_inhibited(1);  // 0x180142d4: the idle notice
        }
    }

    void load_save_start() {
        set_phase(start_file(SAVE_FILE_NAME, false) == FILE_HANDLE_NONE ? LOAD_BACKUP_START
                                                                        : LOAD_SAVE_WAIT);
    }

    void load_save_wait() {
        if (finish_file()) {
            if (save_buffer_valid(game_state::SAVE_SIZE)) {
                adopt_save_buffer();
                set_phase(RESET_SCORES);
                save_reset(0, 0, 1);
                copy_screen_byte_if_saved();
            } else {
                set_phase(LOAD_BACKUP_START);
            }
        }
        if (static_cast<uint32_t>(app2_state().phase) == LOAD_BACKUP_START) {
            load_backup_start();  // the original fell straight through
        }
    }

    void load_backup_start() {
        if (start_file(BACKUP_FILE_NAME, false) != FILE_HANDLE_NONE) {
            set_phase(LOAD_BACKUP_WAIT);
            return;
        }
        set_phase(RESET_SCORES);
        save_reset(1, 1, 1);
        copy_screen_byte_if_saved();
    }

    void load_backup_wait() {
        if (!finish_file()) {
            return;
        }
        if (save_buffer_valid(4)) {
            adopt_save_buffer();
        } else {
            save_reset(1, 1, 0);
        }
        set_phase(RESET_SCORES);
        save_reset(0, 0, 1);
        copy_screen_byte_if_saved();
    }

    void reset_scores() {
        libc::memory_clear(SCORE_TABLE, SCORE_TABLE_BYTES);  // memclr
        app2_state().word_08 = 0;
        app2_state().score_entry = 0;
        score_entries_begin();
        score_file_begin(GAME_STATE);
        set_phase(LOAD_SCORE_ENTRIES);
    }

    void load_score_entries() {
        if (game_state_block().loaded[game_state::LOADED_FLAG] != 1) {
            assert_trap(0x1800f150u);
        }
        const uint32_t pointer = play_state().pointer_7d0;
        if (file_object_written(as_file_object(as_file_record(pointer).object),
                                SCORE_ENTRY_TARGET) == 0) {
            return;
        }
        const uint32_t entry = static_cast<uint32_t>(static_cast<int8_t>(
            static_cast<uint8_t>(game_state_block().loaded[game_state::LOADED_ENTRY]) + 1));
        play_state().byte_7c8 = static_cast<uint8_t>(entry);
        if (static_cast<int32_t>(entry) <
            static_cast<int32_t>(game_state_block().loaded[game_state::LOADED_ENTRY_COUNT])) {
            const FileEntry& row = table_entry<FileEntry>(SCORE_TABLE, app2_state().score_entry);
            file_record_transfer(as_file_record(pointer), row.chunks[entry], row.sizes[entry]);
            return;
        }
        file_record_close(as_file_record(pointer));
        const uint32_t next = app2_state().score_entry + 1;
        app2_state().score_entry = next;
        if (next >= SCORE_ENTRY_COUNT) {
            set_phase(LOAD_RESOURCES);
        } else {
            score_file_begin(GAME_STATE);
        }
    }

    // Phase 7: open the resource packs and prepare the screen that will run first.
    void load_resources() {
        play_state().byte_7be = static_cast<uint8_t>(0);
        const uint32_t handle = pack_open(PACK_NAME_MAIN, 0, language_code());
        game_state_block().pack_handle = handle;
        if (handle == 0) {
            assert_trap(0x1800f248u);
        }
        PackRecord& pack = as_pack(handle);
        pack.largest = game_state_block().word_28;
        pack.scratch = 0x180415e4u;

        // The glyph sheet and its record depend on the language.
        const bool alternative_glyphs = static_cast<uint32_t>(menu_state().language) == 10;
        image_from_resource(as_image(TITLE_IMAGE + 0x3c), 1, pack, alternative_glyphs ? 1u : 0u, 1);
        const uint32_t object_at = font_create(alternative_glyphs ? 0x16bu : 0x5bu);
        game_state_block().object_82bac = object_at;
        if (object_at == 0) {
            assert_trap(alternative_glyphs ? 0x1800f298u : 0x1800f2e4u);
        }
        FontRecord& object = as_font(object_at);
        object.cell_width = alternative_glyphs ? 0x15 : 0x16;
        object.cell_height = alternative_glyphs ? 0x17 : 0x1d;
        object.line_height = alternative_glyphs ? 0x15 : 0x1b;
        const uint32_t record_index = alternative_glyphs ? 2 : 0;

        if (resource_load(as_pack(game_state_block().pack_handle), 0x22, object.widths,
                          object.widths_size) == 0) {
            result = RESULT_SUSPEND;
            return;
        }
        if (resource_load(as_pack(game_state_block().pack_handle), 0x23, object.advances,
                          object.advances_size) == 0) {
            result = RESULT_SUSPEND;
            return;
        }
        libc::memory_copy(field_address(object, offsetof(FontRecord, sheet.texture_index)),
                          RECORD_TABLE + record_index * RECORD_SIZE, RECORD_SIZE);
        object.sheet.width = guest<uint32_t>(RECORD_B_BLOCK + 0xa60);  // the glyph sheet's size
        object.sheet.height = guest<uint32_t>(RECORD_B_BLOCK + 0xa64);
        object.sheet.variant = 1;
        object.sheet.texture_index = object.sheet.texture_index + 1;

        game_state_block().object_24 = random_create(RESOURCE_ID_A);
        if (game_state_block().object_24 == 0) {
            assert_trap(0x1800f3d4u);
        }
        as_random(game_state_block().object_24).seed =
            random_next(game_state_block().object_24, RESOURCE_ID_A);
        fonts_load();

        open_course_packs();
        course_sounds_load();
        if (static_cast<uint32_t>(play_state().byte_81a) == 0) {
            text_block().byte_72b = static_cast<uint8_t>(0xff);
            music_start(0);
        }
        const int32_t course = menu_state().course;
        if (course != 0 && course != 1 && course != 2) {
            assert_trap(0x1800f59cu);
        }
        ImageRecord& block = as_image(GAME_STATE + game_state::BLOCK_84CE0);
        image_apply(block, 0,
                    as_pack(game_state_block().pack_course[static_cast<uint32_t>(course)]),
                    RESOURCE_ID_COURSE_BLOCK);
        block.texture_index = block.texture_index + 2;
        course_images_load();
        enter_first_screen();
        game_state_block().save_write_started = static_cast<uint8_t>(1);
        game_state_block().save_write_second = static_cast<uint8_t>(0);
        set_phase(RUN_SCREEN);  // the save is written later, when something asks for phase 8
    }

    // Open the course's packs; which ones depends on the selected course.
    void open_course_packs() {
        const uint32_t root = game_state_block().word_28;
        // `field` is where the opened pack's handle goes, as a guest address: it is a field of a
        // packed overlay, and a reference to one of those is not portable (aarch64-none-elf-g++
        // refuses to bind one).
        const auto open = [&](uint32_t name, uint32_t field_at, uint32_t trap, uint32_t previous) {
            as_pack(previous).largest = root;
            as_pack(previous).scratch = 0x180415e4u;
            uint32_t& field = guest<uint32_t>(field_at);
            field = pack_open(name, 0, 0);
            if (field == 0) {
                assert_trap(trap);
            }
            return field;
        };
        const auto pack_sheets_at = [](size_t index) {
            return GAME_STATE + static_cast<uint32_t>(offsetof(GameState, pack_sheets)) +
                   static_cast<uint32_t>(index) * 4;
        };
        const int32_t course = static_cast<int32_t>(menu_state().course);
        uint32_t pack;
        if (course == 0) {
            pack = pack_open(PACK_NAME_C00, 0, language_code());
            game_state_block().pack_course[0] = pack;
            if (pack == 0) {
                assert_trap(0x1800f450u);
            }
            pack = open(PACK_NAME_SHEETS, pack_sheets_at(0), 0x1800f480u, pack);
        } else if (course == 1) {
            pack = pack_open(PACK_NAME_C01, 0, language_code());
            game_state_block().pack_course[1] = pack;
            if (pack == 0) {
                assert_trap(0x1800f4c8u);
            }
            pack = open(PACK_NAME_C01_SHEETS, pack_sheets_at(1), 0x1800f4f8u, pack);
        } else {
            pack = pack_open(PACK_NAME_C02, 0, language_code());
            game_state_block().pack_course[2] = pack;
            if (pack == 0) {
                assert_trap(0x1800f520u);
            }
            pack = open(PACK_NAME_C02_SHEETS, pack_sheets_at(2), 0x1800f598u, pack);
        }
        as_pack(pack).largest = root;
        as_pack(pack).scratch = 0x180415e4u;
    }

    // After the resources: run the enter routine of the screen that starts.
    void enter_first_screen() {
        if (static_cast<uint32_t>(play_state().byte_819) == 0) {
            text_block().hole = static_cast<uint8_t>(0);
            if (static_cast<uint32_t>(menu_state().game_mode) == 2) {  // practice hole
                text_block().menu_return_row = static_cast<uint8_t>(0xff);
                hole_select_enter();
                wheel_slots_clear();
            } else if (static_cast<uint32_t>(play_state().byte_81a) == 0) {
                title_enter();
            } else {
                course_start(0);
            }
            return;
        }
        game_state_block().save_data_byte_4f = static_cast<uint8_t>(0);
        const uint32_t screen = screen_id();
        if (screen > 12) {
            assert_trap(0x1800f664u);
        }
        if (screen == 0) {
            title_enter();
        } else if (screen == 11) {
            course_start(0);
        } else if (screen == 12) {
            // nothing: straight on
        } else if (const ScreenEnter enter = screen_enter(screen)) {
            enter();
        } else {
            assert_trap(0x1800f664u);
        }
    }

    void write_save_start() {
        uint32_t name;
        if (static_cast<uint32_t>(game_state_block().save_write_second) != 0) {
            name = BACKUP_FILE_NAME;
        } else {
            if (static_cast<uint32_t>(game_state_block().loaded[game_state::LOADED_FLAG]) == 0) {
                play_state().byte_7be = static_cast<uint8_t>(2);
            }
            name = SAVE_FILE_NAME;
        }
        if (start_file(name, true) != FILE_HANDLE_NONE) {
            set_phase(WRITE_SAVE_WAIT);
            return;
        }
        after_write();
    }

    void write_save_wait() {
        if (finish_file()) {
            after_write();
        }
    }

    // Both save files written (or failed): back to the screens; otherwise write the second.
    void after_write() {
        if (static_cast<uint32_t>(game_state_block().save_write_second) != 0) {
            play_state().byte_7be = static_cast<uint8_t>(0);
            set_phase(RUN_SCREEN);
            return;
        }
        game_state_block().save_write_second = static_cast<uint8_t>(1);
        set_phase(WRITE_SAVE_START);
    }

    uint32_t run() {
        if (static_cast<uint32_t>(app2_state().exiting) != 0) {
            return RESULT_EXITING;
        }
        switch (static_cast<uint32_t>(app2_state().phase)) {
        case RUN_SCREEN:
            run_screen();
            break;
        case LOAD_SAVE_START:
            load_save_start();
            break;
        case LOAD_SAVE_WAIT:
            load_save_wait();
            break;
        case LOAD_BACKUP_START:
            load_backup_start();
            break;
        case LOAD_BACKUP_WAIT:
            load_backup_wait();
            break;
        case RESET_SCORES:
            reset_scores();
            break;
        case LOAD_SCORE_ENTRIES:
            load_score_entries();
            break;
        case LOAD_RESOURCES:
            load_resources();
            break;
        case WRITE_SAVE_START:
            write_save_start();
            break;
        case WRITE_SAVE_WAIT:
            write_save_wait();
            break;
        default:
            assert_trap(0x1800f788u);
        }
        return result;
    }
};

}  // namespace

// 0x1800ecd8 — one step of the start-up and flow state machine.
uint32_t flow_step(uint32_t milliseconds) {
    Flow flow(milliseconds);
    return flow.run();
}

// 0x1801247c — queue the score entries the title needs: a dozen (text, entry) pairs, the text
// chosen by language, the set by course (courses 0, 1 and the rest each name their own four).
void score_entries_begin() {
    uint32_t text = 1;
    switch (static_cast<uint32_t>(menu_state().language)) {
    case 1:
        text = 2;
        break;
    case 3:
        text = 3;
        break;
    case 4:
        text = 4;
        break;
    case 5:
        text = 5;
        break;
    case 8:
        text = 6;
        break;
    case 9:
        text = 7;
        break;
    case 10:
        text = 8;
        break;
    case 13:
        text = 9;
        break;
    case 14:
        text = 10;
        break;
    case 16:
        text = 11;
        break;
    default:
        break;
    }
    const int32_t course = static_cast<int32_t>(menu_state().course);
    const uint32_t own_a = course == 0 ? 0xa : course == 1 ? 0xc : 0xe;
    const uint32_t own_b = course == 0 ? 2 : course == 1 ? 4 : 6;
    const uint32_t requests[12][2] = {{0, 8},     {0, 9},         {0, 0},        {0, 1},
                                      {text, 0},  {text, 1},      {0, own_a},    {0, own_a + 1},
                                      {0, own_b}, {0, own_b + 1}, {text, own_b}, {text, own_b + 1}};
    for (uint32_t i = 0; i < 11; ++i) {
        score_entry_request(requests[i][0], requests[i][1]);
    }
    score_entry_request(requests[11][0], requests[11][1]);  // a tail call in the original
}

// 0x180041fc — a fresh save: the options back to their defaults when `reset_options`, and the
// record cleared — unless a round is in progress and `even_in_progress` is off — with one
// course unlocked, no best rounds and the statistics at zero. `forget_screen` also drops the
// remembered screen id. A course being resumed sets the current course from the record.
void save_reset(uint32_t reset_options, uint32_t even_in_progress, uint32_t forget_screen) {
    if (reset_options != 0) {
        options_state().music = static_cast<int8_t>(2);
        options_state().sound_fx = static_cast<uint8_t>(1);
        options_state().clock_battery = static_cast<uint8_t>(1);
        options_state().player_gender = static_cast<uint8_t>(0);
    }
    const uint32_t record = GAME_STATE + game_state::SAVE_DATA;
    if (even_in_progress != 0 || save_record().in_progress == 0) {
        libc::memory_clear(record, game_state::SAVE_DATA_SIZE);
        screen_state().save_fresh = static_cast<uint8_t>(1);
        screen_state().byte_d85 = static_cast<uint8_t>(0);
        screen_state().course_loading = static_cast<uint8_t>(0);
        screen_state().courses_unlocked = static_cast<uint8_t>(1);
        text_block().byte_745 = static_cast<uint8_t>(0);
        for (uint32_t course = 0; course < 3; ++course) {
            screen_state().best_round[course] = NO_BEST_ROUND;
        }
        screen_state().holes_played = 0;
        screen_state().holes_in_one = 0;
        screen_state().statistic_da8 = 0;
        save_record().magic = SAVE_MAGIC;
        game_state_block().save_magic = SAVE_MAGIC;
    }
    if (forget_screen != 0) {
        screen_state().id = static_cast<uint8_t>(0);
    }
    if (play_state().byte_819 != 0) {
        text_block().byte_745 = static_cast<uint8_t>(save_record().course);
    }
}

// 0x180135f4 — queue one score entry (language text, entry index) as the next file to load,
// if the file table has it; its size counts towards the loading total.
void score_entry_request(uint32_t text, uint32_t entry) {
    if (text >= 12 || entry >= 16) {
        assert_trap(0x18013600u);
    }
    const FileKind& kind = file_kind(text, entry);
    if (kind.name == 0) {
        return;
    }
    FileEntry& row = table_entry<FileEntry>(FILE_TABLE, app2_state().word_08);
    row.text = text;
    app2_state().word_08 = app2_state().word_08 + 1;
    row.entry = entry;
    play_state().word_7c0 = play_state().word_7c0 + kind.size;
}

// 0x1800e644 — the ten sound-slot flags from a loader's result table (8 bytes an entry, the
// status byte first): set where the load finished, cleared where it did not.
void slot_flags_from_table(uint32_t unused, uint32_t table, uint32_t count) {
    (void)unused;
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(count); ++i) {
        play_state().sound_enabled[i] =
            static_cast<uint8_t>(table_entry<SoundResult>(table, i).status != 0 ? 0 : 1);
    }
}

// 0x18013fc0 — tell the flags object the app is idle, with the tick's answer.
void idle_suspend_notify(uint32_t answer) {
    flags_object().idle = 1;
    flags_object().idle_answer = answer;
}

// 0x1800cc30 — the two-letter code of the current language, for the text resources' names.
uint32_t language_code() {
    switch (static_cast<uint32_t>(menu_state().language)) {
    case 1:
        return LANGUAGE_CODES[0];  // da
    case 3:
        return LANGUAGE_CODES[1];  // de
    case 4:
        return LANGUAGE_CODES[2];  // es
    case 5:
        return LANGUAGE_CODES[3];  // fi
    case 8:
        return LANGUAGE_CODES[4];  // fr
    case 9:
        return LANGUAGE_CODES[5];  // it
    case 10:
        return LANGUAGE_CODES[6];  // ja
    case 13:
        return LANGUAGE_CODES[7];  // nl
    case 14:
        return LANGUAGE_CODES[8];  // no
    case 16:
        return LANGUAGE_CODES[9];  // sv
    default:
        return LANGUAGE_CODES[10];  // en
    }
}

void f_1800e644(Cpu& cpu) {
    slot_flags_from_table(cpu.r[0], cpu.r[1], cpu.r[2]);
}

}  // namespace minigolf::game
