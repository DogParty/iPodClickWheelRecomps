// The save file and the statistics: the best rounds kept up to date with the player's name,
// the "stats" file written through the async file layer, and leaving the app.
#include "save.h"

#include "async_request.h"
#include "calling.h"
#include "game_state.h"
#include "init.h"
#include "libc.h"
#include "records.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "state.h"

namespace minigolf::game {

void course_state_save(uint32_t resumable);
uint32_t sound_slots_release_all();
void tracked_free_all();
void pack_close(PackRecord& pack_at);
void texture_release(ImageRecord& image);
void font_destroy(uint32_t at);

namespace {

constexpr uint32_t RECORD = GAME_STATE + game_state::SAVE_DATA;
// The record's best rounds: each a score word and the name it was made under.
struct Best {
    uint32_t score, current, name;
    bool higher_is_better;
};
constexpr Best BESTS[] = {{0x9c, 0x20, 0x7c, false},
                          {0xc4, 0x24, 0xa4, false},
                          {0xec, 0x2c, 0xcc, true},
                          {0x114, 0x30, 0xf4, true},
                          {0x13c, 0x34, 0x11c, true}};
constexpr uint32_t PLAYER_NAME_IN_RECORD = 0x52;
constexpr uint32_t GLYPH_CODES_WIDE =
    GAME_STATE + game_state::SETTINGS + 0x7de;  // halfword per code

constexpr uint32_t STATS_ROOTS = 0x1801'bdf8, STATS_ROOT_BY_REGION = 0x1801'8fd4,
                   STATS_SUFFIX = 0x1801'42a0;
constexpr uint32_t STATS_HEADER = 0x1801'9a78, STATS_RECORD_DATA = GAME_STATE + 0x82df8;
constexpr uint32_t STATS_RECORD_SIZE = 40;
constexpr uint32_t SAVE_NAME = 0x1801'150c;  // "jdmgp.sav"
constexpr uint32_t TEXTURE_COUNT = 0x21;

}  // namespace

// 0x1800cb9c — a string into the glyph codes the font draws it with: for the UTF-16 language
// each halfword, otherwise each byte, looked up in the code table.
void string_translate(uint32_t destination, uint32_t source) {
    if (static_cast<uint32_t>(menu_state().language) == LANGUAGE_WIDE) {
        for (uint32_t i = 0;; ++i) {
            const uint32_t code =
                static_cast<uint32_t>(static_cast<int32_t>(guest<int16_t>(source + i * 2)));
            guest<uint16_t>(destination + i * 2) = guest_array<uint16_t>(GLYPH_CODES_WIDE)[code];
            if (guest<uint16_t>(source + i * 2) == 0) {
                return;
            }
        }
    }
    for (uint32_t i = 0;; ++i) {
        const uint32_t code =
            static_cast<uint32_t>(static_cast<int32_t>(guest<int8_t>(source + i)));
        guest<uint16_t>(destination + i * 2) = guest_array<uint16_t>(GLYPH_CODES_WIDE)[code];
        if (guest<int8_t>(source + i) == 0) {
            return;
        }
    }
}

// 0x1800e68c — the round's statistics against the record's bests: a better one (lower for
// the first two, higher for the rest, or any when none is set) replaces it with the
// player's name.
void statistics_update() {
    for (const Best& best : BESTS) {
        uint32_t& score_word = guest<uint32_t>(RECORD + best.score);
        const int32_t score = static_cast<int32_t>(score_word);
        const int32_t current = static_cast<int32_t>(guest<uint32_t>(RECORD + best.current));
        const bool better = best.higher_is_better ? score > current : score < current;
        if (!better && score != 0) {
            continue;
        }
        score_word = static_cast<uint32_t>(current);
        string_translate(RECORD + best.name, RECORD + PLAYER_NAME_IN_RECORD);
    }
}

// 0x180141f4 — the statistics file's path (a root by the device's region, then "stats"), and
// the bytes to write copied from the record: its eight-byte header and its entries.
void stats_path_build(StatsRecord& record, uint32_t path) {
    const uint32_t region = static_cast<uint32_t>(settings_language());
    uint32_t root = 0;
    if (region < 0x19) {
        root = guest_array<uint8_t>(STATS_ROOT_BY_REGION)[region];
        if (root >= 0xb) {
            root = 0;
        }
    }
    libc::string_copy(path, guest_array<uint32_t>(STATS_ROOTS)[root]);
    libc::string_append(path, STATS_SUFFIX);
    const uint32_t bytes = record.count * STATS_RECORD_SIZE + 8;
    const uint32_t buffer = libc::heap_allocate(bytes);
    device_block().stats_buffer = buffer;
    libc::memory_copy(buffer, address_of(record), 8);
    libc::memory_copy(buffer + 8, record.data, bytes - 8);
}

// 0x18014dc8 — write the statistics file, if the device allows it. Returns 1, or -1 when not.
uint32_t stats_write(StatsRecord& record) {
    GuestScratch frame(4 * 4 + 0x108);
    if (device_block().stats_enabled == 0) {
        return 0xffffffffu;
    }
    const uint32_t request = frame.at(0), path = frame.at(8);
    stats_path_build(record, path);
    simple_file_open(as_simple_file(request), 2, path);
    simple_file_write(as_simple_file(request), device_block().stats_buffer,
                      record.count * STATS_RECORD_SIZE + 8);
    libc::heap_free(device_block().stats_buffer);
    simple_file_close(as_simple_file(request));
    return 1;
}

// 0x1800cd5c — the statistics written: the built-in header and the record's entries.
void stats_save() {
    GuestScratch frame(4 * 4);  // the pushed argument slots hold the record
    const uint32_t record = frame.at(4 * 0);
    as_stats_record(record).header = guest_array<uint32_t>(STATS_HEADER)[0];
    as_stats_record(record).count = guest_array<uint32_t>(STATS_HEADER)[1];
    as_stats_record(record).data = STATS_RECORD_DATA;
    stats_write(as_stats_record(record));
}

// 0x18011414 — leave the app: the round marked resumable if a single-player hole was in play,
// the save file written, the statistics updated and written, then every texture, font, pack,
// sound and allocation released. Answers 3 to the state machine.
uint32_t app_exit() {
    GuestScratch frame(4 * 8);
    const uint32_t request = frame.at(4 * 0);
    if (static_cast<uint32_t>(screen_state().byte_d85) == 0 &&
        static_cast<uint32_t>(screen_state().id) == 0xb &&
        static_cast<uint32_t>(menu_state().game_mode) == 0) {
        screen_state().byte_d85 = static_cast<uint8_t>(1);
    }
    course_state_save(1);
    simple_file_open(as_simple_file(request), 1, SAVE_NAME);
    simple_file_write(as_simple_file(request), GAME_STATE + game_state::SAVE_BUFFER, 0x148);
    statistics_update();
    stats_save();
    app2_state().exiting = static_cast<uint8_t>(1);
    for (int32_t t = static_cast<int32_t>(TEXTURE_COUNT) - 1; t >= 0; --t) {
        texture_release(as_image(TITLE_IMAGE + static_cast<uint32_t>(t) * TEXTURE_TABLE_STRIDE));
    }
    ScreenState& screen = screen_state();
    for (const uint32_t font : {screen.small_font, screen.text_layout, screen.font_object}) {
        if (font != 0) {
            font_destroy(font);
        }
    }
    if (game_state_block().pack_handle != 0) {
        PackRecord& pack = as_pack(game_state_block().pack_handle);
        pack.scratch = 0;
        pack_close(pack);
    }
    sound_slots_release_all();
    tracked_free_all();
    simple_file_close(as_simple_file(request));
    return 3;
}

}  // namespace minigolf::game
