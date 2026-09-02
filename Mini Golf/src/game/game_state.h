// Names for the game's recovered globals and structures, shared by every decompiled function.
//
// As functions are decompiled, the fields and globals they use are named here — once — so the
// same address is never called two things in two files. Nothing is declared until it has been
// understood; an entry carries the guest address it stands for and where that was established.
//
// Conventions:
//   * guest pointers are `uint32_t` and read through the accessors in runtime/memory.h;
//   * a struct layout is recorded as named offsets until the whole object is understood, then
//     promoted to a `struct` with a guest-memory view;
//   * a name followed by "(inferred)" describes observed use, not a proven meaning.
#pragma once

#include <cstdint>

namespace minigolf::game {

// --- The shape of the game itself ------------------------------------------------------------
// Sizes and counts that describe Mini Golf rather than the machine it ran on. Every file that
// needs one takes it from here, so the same quantity is never named twice.

// The screen, in pixels. `gfx::SCREEN_WIDTH`/`SCREEN_HEIGHT` are the pipeline's own copy.
constexpr uint32_t SCREEN_WIDTH = 320, SCREEN_HEIGHT = 240;
constexpr int32_t SCREEN_CENTRE_X = static_cast<int32_t>(SCREEN_WIDTH) / 2;

// The three courses the player can choose, and the eighteen holes each of them has.
constexpr uint32_t COURSE_COUNT = 3;
constexpr uint32_t HOLES_PER_COURSE = 18;

// How a round is played. The mode decides whether scores are kept per player and whether the
// course can be saved.
constexpr uint32_t MODE_SINGLE_PLAYER = 0, MODE_PASS_N_PLAY = 1, MODE_PRACTICE_HOLE = 2;

// --- The firmware's per-task context, passed to every vector call (app.cpp) -----------------
// One 0x400-byte object; the frame vector receives (context, context + 0x100).
namespace context {
constexpr uint32_t STATE = 0x00;    // byte; the game copies the answer's state here each frame
constexpr uint32_t ANSWER = 0x100;  // the second half: what the game reports back
}  // namespace context

namespace answer {
constexpr uint32_t STATE = 0x00;  // byte; 5 = asks to suspend, 6 = suspended (inferred)
}  // namespace answer

// --- Click-wheel input state, a global at 0x180379f8 (app.cpp) -------------------------------
constexpr uint32_t INPUT_STATE = 0x1803'79f8;
namespace input {
// held one frame, released on the next
constexpr uint32_t FLAGS = 0x14;  // the button-flags word (0x18037a0c): see below

// FLAGS bits. The five buttons are set by the firmware (here: the frame pump) for one frame;
// the frame vector maintains the two wheel bits itself.
constexpr uint32_t FLAG_SELECT = 0x01;
constexpr uint32_t FLAG_PREVIOUS = 0x02;
constexpr uint32_t FLAG_PLAY = 0x04;
constexpr uint32_t FLAG_NEXT = 0x08;
constexpr uint32_t FLAG_MENU = 0x10;
constexpr uint32_t FLAG_WHEEL_EVENT = 0x20;   // a wheel sample arrived this frame
constexpr uint32_t FLAG_WHEEL_TAPPED = 0x40;  // the wheel was touched afresh more than once in the
                                              // last 16 frames (inferred: a tap detector)
}  // namespace input

// Sixteen-entry rings over the last 16 frames, both indexed by input::RING_INDEX: whether the
// frame had wheel contact, and whether that contact was new (no contact the frame before).
constexpr uint32_t WHEEL_CONTACT_RING = 0x180e'1a18;
constexpr uint32_t WHEEL_TOUCH_BEGIN_RING = WHEEL_CONTACT_RING + 0x40;
constexpr uint32_t WHEEL_RING_SIZE = 16;

// --- Program start-up (app.cpp) ------------------------------------------------------------
constexpr uint32_t BSS_START = 0x1803'7a1c;  // zero-filled before anything runs
constexpr uint32_t BSS_SIZE = 0xaa108;

// Event words from InputEvents #0: bit 30 marks an event, the low byte is the wheel position.
constexpr uint32_t EVENT_PRESENT = 0x4000'0000;

// Where the wheel position goes (input.cpp): 16.16 fixed point, kept in two places.
constexpr uint32_t WHEEL_POSITION = 0x1804'030c;
constexpr uint32_t WHEEL_POSITION_COPY = WHEEL_POSITION + 0x208;

// The frame's input as the game logic reads it (input.cpp): button flags, the event list, and
// two words the update copies along with them (meaning not yet established).
constexpr uint32_t INPUT_SNAPSHOT = 0x1804'050c;
namespace snapshot {
constexpr uint32_t FLAGS = 0x04;
constexpr uint32_t WHEEL_POSITION = 0x08;  // 16.16, the copy wheel_position_update keeps here
constexpr uint32_t COPY_SIZE = 0x1c;       // bytes game_update copies out before clearing
}  // namespace snapshot

// --- The application object, a global at 0x1801be2c (app.cpp) --------------------------------
constexpr uint32_t APP = 0x1801'be2c;
namespace app {
constexpr uint32_t STATE = 0x00;    // byte; non-zero once the game proper is running (inferred)
constexpr uint32_t CONTEXT = 0x04;  // the firmware context of the current frame
constexpr uint32_t ANSWER = 0x08;
constexpr uint32_t MODE = 0x0c;  // 2 = a mode that starts the render pipeline (inferred)
}  // namespace app

// The step's private copy of the input snapshot (same layout), and per-button hold times in
// milliseconds, one word per flag bit 0..5 (game_step).
constexpr uint32_t STEP_INPUT = 0x1804'13fc;
constexpr uint32_t BUTTON_HOLD_TIMES = 0x1804'13e8;
constexpr uint32_t BUTTON_HOLD_COUNT = 6;

// --- The game state, one 0x8faf4-byte object at 0x180415b8 (init.cpp and below) ---------------
// armcc addresses its parts as base + large constant + small offset, which is how the sub-blocks
// below were found. Fields are named as they are understood; most are still "(inferred)".
constexpr uint32_t GAME_STATE = 0x1804'15b8;
constexpr uint32_t GAME_STATE_SIZE = 0x8faf4;
constexpr uint32_t TITLE_IMAGE =
    GAME_STATE + GAME_STATE_SIZE;  // 286×341 texels, loaded at start-up
constexpr uint32_t TITLE_IMAGE_WIDTH = 0x11e, TITLE_IMAGE_HEIGHT = 0x155;
constexpr uint32_t TEXTURE_TABLE_STRIDE = 60;     // TITLE_IMAGE is the first of a table of textures
constexpr uint32_t VERSION_STRING = 0x1804'1578;  // "1.0.0", copied from the image at start-up
constexpr uint32_t VERSION_LITERAL = 0x1801'2458;

// A second application block at 0x1801be5c (state_machine_step and init.cpp).
constexpr uint32_t APP2 = 0x1801'be5c;
namespace app2 {
constexpr uint32_t PHASE = 0x03;  // byte 0..9: which screen the state machine runs
}  // namespace app2
constexpr uint32_t APP2_FLAGS_POINTER = 0x1801'be34;  // -> object; +0x20 |= bits (0x18014ce0)

// The five sound slots (a flag byte each at 0x1801bdcc and a 64-handle table each at
// 0x18040ee8 in the original) now live on the host: SoundSlots in sounds.h.
constexpr uint32_t SLOT_COUNT = 5;

// The sixteen files the game keeps in memory (0x68 bytes each): a name record, up to ten chunk
// pointers and their sizes, and a read cursor. The score entries are the first of them.
constexpr uint32_t FILE_TABLE = 0x180d'1340, FILE_COUNT = 16, FILE_SIZE = 0x68;
namespace file {
constexpr uint32_t NAME = 0x8;     // -> record: +0 name string, +4 total size
constexpr uint32_t CURSOR = 0x5c;  // -> the next byte to read
constexpr uint32_t OFFSET = 0x60;  // into the current chunk
constexpr uint32_t CHUNK_LIMIT = 10;
}  // namespace file
constexpr uint32_t SCORE_TABLE = FILE_TABLE;

// Two 36-byte default records copied from the image at start-up (0x18006ecc).
constexpr uint32_t DEFAULT_RECORD_A = 0x1803'3f64, DEFAULT_RECORD_B = 0x1803'3f88;
constexpr uint32_t RECORD_A = 0x180c'63cc, RECORD_B_BLOCK = 0x180c'55b8;
constexpr uint32_t RECORD_SIZE = 0x24;
constexpr uint32_t LANGUAGE_KEY = 0x1801'8b7c;  // "Language", for Settings #0

// --- Offsets into the game state block -------------------------------------------------------
// The sub-blocks it is divided into, and the fields the flow state machine reaches by offset
// rather than through a structure in state.h.
namespace game_state {
constexpr uint32_t COURSE_TABLE =
    0x84000;  // 11 records of 60 bytes; +0xa10 in each set to -1 at start-up
constexpr uint32_t COURSE_RECORD_SIZE = 60;
// Slots in that table — more than the three courses the player sees; start-up clears them all.
constexpr uint32_t COURSE_RECORD_COUNT = 11;
constexpr uint32_t TEXT = 0x85000;      // text / font block
constexpr uint32_t SETTINGS = 0x8f000;  // options and audio levels
constexpr uint32_t SETTINGS_BYTE_7BE = 0x7be, SETTINGS_BYTE_819 = 0x819, SETTINGS_BYTE_81A = 0x81a,
                   SETTINGS_BYTE_81C = 0x81c;  // set to 1, 1, 0, 1 at start-up
constexpr uint32_t PACK_HANDLE = 0x04;         // the "jdmg" resource pack
constexpr uint32_t SCREEN_BYTE_9A = 0x9a;
constexpr uint32_t SCREEN_ID = 0xcd;     // signed byte: which screen runs in phase 0
constexpr uint32_t SAVE_DATA = 0x82d80;  // 0x144 bytes, the live save game
constexpr uint32_t SAVE_DATA_SIZE = 0x144;
namespace save {
constexpr uint32_t COURSE = 0x38;  // signed byte: the course in progress
}  // namespace save
constexpr uint32_t SAVE_BUFFER = 0x82ec4;  // 0x148 bytes, the file image being read or written
constexpr uint32_t SAVE_SIZE = 0x148;
constexpr uint32_t SAVE_COPY_SIZE = 0x144;
constexpr uint32_t SAVE_MAGIC_END = 0x140;  // the magic sits at +0 and +0x140 of the buffer
constexpr uint32_t BLOCK_84CE0 = 0x84ce0;   // the background image (draw.h image layout)
constexpr uint32_t LOADED_FLAG = 0xbe;
constexpr uint32_t LOADED_ENTRY = 0xc8;
constexpr uint32_t LOADED_ENTRY_COUNT = 0xc9;
}  // namespace game_state
constexpr uint32_t SAVE_MAGIC = 0xc0de'babe;
constexpr uint32_t NO_BEST_ROUND = 0xb5;                // a best round not yet set
constexpr uint32_t SAVE_FILE_NAME = 0x1800'f1f8;        // "jdmgp.sav"
constexpr uint32_t BACKUP_FILE_NAME = 0x1800'f208;      // "jdmgp2.sav"
constexpr uint32_t PACK_NAME_MAIN = 0x1800'f524;        // "jdmg"
constexpr uint32_t PACK_NAME_C01_SHEETS = 0x1800'f55c;  // "c01sheets"
constexpr uint32_t PACK_NAME_C02 = 0x1800'f568;         // "c02"
constexpr uint32_t PACK_NAME_C02_SHEETS = 0x1800'f794;  // "c02sheets"
constexpr uint32_t PACK_NAME_C01 = 0x1800'f548, PACK_NAME_C00 = 0x1800'f54c,
                   PACK_NAME_SHEETS = 0x1800'f550;
constexpr uint32_t RESOURCE_ID_A = 0x0a6b'99cd;
constexpr uint32_t RECORD_TABLE = 0x1803'3ff4;  // 36-byte records; index chosen by glyph sheet
constexpr uint32_t SCORE_ENTRY_TARGET = 0x1801'be60;
constexpr uint32_t IDLE_SUSPEND_MS = 0x3a980;  // 240 s without input: answer 5
constexpr uint32_t IDLE_NOTICE_MS = 0x1d4c0;   // 120 s without input: answer 1

// --- The game's file service (files.cpp) ---------------------------------------------------
// A heap object of ten operation slots, reached through FILE_SERVICE_POINTER; created on first
// use. Each slot holds a copy of the caller's request, the AsyncFileIO request it issues, and
// the operation's progress.
constexpr uint32_t FILE_SERVICE_POINTER = 0x1801'be28;
constexpr uint32_t FILE_SERVICE_SIZE = 0xf2c;
namespace file_service {
constexpr uint32_t SLOT_COUNT = 10;
constexpr uint32_t SLOT_SIZE = 0x184;
}  // namespace file_service
namespace file_slot {
constexpr uint32_t ID = 0x00;                 // the slot's index; what callers get as a handle
constexpr uint32_t REQUEST = 0x08;            // copy of the caller's request (file_request below)
constexpr uint32_t STATUS = 0x120;            // bytes transferred (set by the completion)
constexpr uint32_t TRANSFER_REQUEST = 0x128;  // the AsyncFileIO request object for the transfer
constexpr uint32_t ASYNC_REQUEST = 0x16c;     // the Operation record (see `operation`)
constexpr uint32_t RESULT = 0x11c;            // the operation's result word, copied on completion
}  // namespace file_slot
// The slot's record of a request in flight (at file_slot::ASYNC_REQUEST) — async_request.cpp.
namespace operation {
constexpr uint32_t FILE_HANDLE = 0x00;  // from the request once an open completes
constexpr uint32_t STATE = 0x04;  // byte: 0 idle, 1 opening, 2 open, 3 closing, 4 transferring
constexpr uint32_t RESULT = 0x08;
constexpr uint32_t CALLBACK = 0x0c;  // slot-level completion and its context
constexpr uint32_t CONTEXT = 0x10;
}  // namespace operation
// The AsyncFileIO request object the game allocates per operation (0x3c bytes) — the same
// layout libeapp reads (request::CALLBACK / CONTEXT at +0x34 / +0x38).
namespace async_request {
constexpr uint32_t STATE = 0x04;  // byte: the operation code
constexpr uint32_t BUFFER = 0x14;
constexpr uint32_t LENGTH = 0x18;
constexpr uint32_t STATUS = 0x20;
constexpr uint32_t RESULT = 0x28;
constexpr uint32_t FILE_HANDLE = 0x2c;
constexpr uint32_t CALLBACK = 0x34;
constexpr uint32_t CONTEXT = 0x38;
}  // namespace async_request
// What file_request_prepare (as_file_request(0x18017864)) builds for begin_read/begin_write.
namespace file_request {
constexpr uint32_t MODE = 0x000;  // byte
constexpr uint32_t NAME = 0x001;  // up to 0xff characters, NUL-terminated at +0x100
constexpr uint32_t NAME_CAPACITY = 0x100;
constexpr uint32_t BUFFER = 0x104;
constexpr uint32_t KIND = 0x108;    // byte; 0 or 1 = a whole-file transfer
constexpr uint32_t OFFSET = 0x10c;  // (inferred: non-zero selects the positioned path)
constexpr uint32_t LENGTH = 0x110;
constexpr uint32_t SIZE = 0x114;
}  // namespace file_request
constexpr uint32_t FILE_READ_COMPLETION = 0x1801'6ee8;  // callbacks the slots register
constexpr uint32_t FILE_OTHER_COMPLETION = 0x1801'6e98;
constexpr uint32_t FILE_HANDLE_INVALID = 0xffff'ffffu;

// --- Screens (screens.cpp, title.cpp, menu.cpp, name_entry.cpp, dialog.cpp) -----------------
// The running screen is an object inside the game state: a handler for button events, a tick,
// a render, and the enter routine to run when it is done; each screen's enter routine installs
// its own. SCREEN_ID (above) says which screen is running; SCREEN_PHASE is the screen's own
// sub-state (slide in, steady, slide out ...).
constexpr uint32_t SCREEN_OBJECT = GAME_STATE + 0x82000;
namespace screen {
constexpr uint32_t FONT_OBJECT = 0xbac;  // from object_create at start-up
constexpr uint32_t ID = 0xdcd;           // byte (SCREEN + 0xcd)
constexpr uint32_t PHASE = 0xdce;        // byte (SCREEN + 0xce)
constexpr uint32_t SAVE_FRESH = 0xd84,
                   BYTE_D85 = 0xd85;  // bytes: the save was just reset; resuming
constexpr uint32_t SAVED_BALL_X = 0xdbc, SAVED_BALL_Y = 0xdc0,
                   SAVED_STATE = 0xdc8;                         // the hole's state for a resume
constexpr uint32_t HOLES_PLAYED = 0xda0, HOLES_IN_ONE = 0xda4;  // words: single-player statistics
constexpr uint32_t PLAYER_NAME = 0xdd2;  // the player's name: 8-bit, or UTF-16 for language 10
}  // namespace screen
// One entry of a menu's item table.
namespace menu_item {
constexpr uint32_t KIND = 0x04;  // 0..6: what selecting it does (menu.cpp)
constexpr uint32_t X = 0x08;     // 16.16 screen position during the slide
constexpr uint32_t Y = 0x0c;     // 16.16
constexpr uint32_t SIZE = 0x18;
}  // namespace menu_item
// Text-block fields the screens use (offsets from GAME_STATE + TEXT).
namespace text {
// The course carousel reuses the same slots as three 0x30-byte slides (course_select.cpp).
constexpr uint32_t SLIDES = 0x1cc, SLIDE_SIZE = 0x30;
constexpr uint32_t SLIDE_PICTURE = 0x0, SLIDE_X = 0x4, SLIDE_Y = 0x8, SLIDE_SPEED = 0x10;
// The score card's picture drop and totals (course.cpp).
constexpr uint32_t SCORE_CARD_PICTURE = 0x2bc, SCORE_CARD_X = 0x2c0, SCORE_CARD_Y = 0x2c4,
                   SCORE_CARD_DY = 0x2cc;
constexpr uint32_t TOTAL_PAR = 0x754, TOTAL_PLAYER1 = 0x758, TOTAL_PLAYER2 = 0x75c;
constexpr uint32_t WORD_200 = 0x200, WORD_204 = 0x204, WORD_20C = 0x20c, WORD_22C = 0x22c,
                   WORD_230 = 0x230, WORD_234 = 0x234, WORD_23C = 0x23c;
constexpr uint32_t BYTE_729 = 0x729, BYTE_72D = 0x72d, BYTE_72E = 0x72e;
constexpr uint32_t HOLE = 0x746;  // byte: the hole to play, 0-based
}  // namespace text
// The menu / screen cursor state at TEXT + 0x700 (the code reaches it both as TEXT + 0x7xx and
// as COURSE_SELECT + 0x2x; these are the same bytes). The name-entry screen reuses it: the
// glyph wheel is a menu whose cursor is the chosen letter.
constexpr uint32_t MENU = GAME_STATE + 0x85700;
namespace menu {
constexpr uint32_t CURSOR = 0x27;  // signed byte: selected item / glyph
constexpr uint32_t COURSE = 0x45;  // signed byte: -1 none, 0, 1, 2
constexpr uint32_t HOLE = 0x46;    // signed byte: the hole being played, 0-based
constexpr uint32_t STROKES_PLAYER1 = 0x60, STROKES_PLAYER2 = 0x88;  // a signed byte per hole
constexpr uint32_t PLAYER_GENDER = 0x74;  // halfword: 0 female, 1 male, for this round
}  // namespace menu
// The three course pictures the carousel shows (course_select.cpp), one per course.
constexpr uint32_t COURSE_PICTURES = GAME_STATE + 0x84e0c;
namespace course_picture {
constexpr uint32_t USE_COUNT = 0x8;  // the pack keeps a reference count; the placeholder has none
constexpr uint32_t WIDTH = 0x2c, HEIGHT = 0x30;
constexpr uint32_t SIZE = 0x3c;
}  // namespace course_picture
// --- The play state: what a hole in progress keeps in the settings block (hole_tick.cpp) ------
constexpr uint32_t PLAY = GAME_STATE + 0x8f000;  // = SETTINGS
namespace play {
constexpr uint32_t METER_VALUE = 0x604, METER_SCALE = 0x608, METER_MAX = 0x60c, METER_STEP = 0x610;
constexpr uint32_t METER_START = 0x614;  // where the meter's sweep begins
constexpr uint32_t METER_SPEED = 0x618;  // how fast it sweeps
constexpr uint32_t STATE = 0x628;        // HoleState
constexpr uint32_t STROKE_START_X = 0x63c, STROKE_START_Y = 0x640;  // 16.16, for undo
constexpr uint32_t VELOCITY_X = 0x648, VELOCITY_Y = 0x64c;          // 16.16 per step
constexpr uint32_t BALL_X = 0x650, BALL_Y = 0x654;                  // 16.16 in tile units
constexpr uint32_t SAVED_FRAMES = 0x668, SAVED_STATE = 0x66c;       // across the hint sequence
constexpr uint32_t SINK_FROM_X = 0x670, SINK_FROM_Y = 0x674, SINK_TO_X = 0x678, SINK_TO_Y = 0x67c;
constexpr uint32_t ELAPSED_MS = 0x688, FRAME_MS = 0x68c;
constexpr uint32_t BYTE_796 = 0x796, PLACE_DIRECTION = 0x797, BYTE_799 = 0x799;
constexpr uint32_t FRAME_COUNTS_A = 0x5f4, FRAME_COUNTS_B = 0x5f8;  // -> frames per object kind
constexpr uint32_t WHEEL_REPEAT = 0x7fc, WHEEL_REPEAT_LIMIT = 0x800, WHEEL_REPEATS = 0x804;
// The panel's grow-in (ui.cpp, page.cpp): a flag, a 16.16 scale and its per-frame step.
constexpr uint32_t PANEL_GROWING = 0x80c, PANEL_SCALE = 0x810, PANEL_SCALE_STEP = 0x814;
// The course's collision tables (pointers) and constants, filled when a hole loads.
constexpr uint32_t FRICTION = 0x548, RESTITUTION = 0x54c;      // 16.16 per step; 8.8
constexpr uint32_t SLOPE_GENTLE = 0x550, SLOPE_STEEP = 0x554;  // the pull of a slope, by steepness
}  // namespace play
constexpr uint32_t TRAIL_X = GAME_STATE + 0x8f694,
                   TRAIL_Y = GAME_STATE + 0x8f714;  // 64 halfwords each
// The players in the round (PLAY + 0x500): who is up, and the animated objects' thresholds.
constexpr uint32_t PLAYERS = GAME_STATE + 0x8f500;
namespace players {
constexpr uint32_t KIND_LIMIT_A = 0xc6, KIND_LIMIT_B = 0xca;  // halfwords
constexpr uint32_t TEE_EDGE_COUNT = 0xba, OBSTACLE_COUNT = 0xc0, WALL_COUNT = 0xc2,
                   PEG_COUNT = 0xc4;
constexpr uint32_t SPRITE_SHEET_COUNT = 0xc8, SPRITE_SHEET_B_COUNT = 0xcc;  // halfwords
constexpr uint32_t TEE_POINT_COUNT = 0xb8, SECTION_2_COUNT = 0xbc;          // halfwords
constexpr uint32_t HOLE_WORDS_LOW = 0x70,
                   HOLE_WORDS_HIGH =
                       0x90;  // 16 halfwords each: the hole data's words at +0x28, split
}  // namespace players
// Which meshes the ball has passed (a byte each), and their rectangles (36-byte entries:
// x, y, width, height words at +0x10; a byte at +0x20 when passing them is a surface).
constexpr uint32_t MESH_SEEN = 0x180e1a00, MESH_VISITS = 0x18037690, MESH_VISIT_COUNT = 0x17;
// Per-player records, 0x28 bytes apart from MENU (so player 0's strokes-by-hole are MENU + 0x60).
constexpr uint32_t PLAYER_STRIDE = 0x28;
namespace player {
constexpr uint32_t STROKES_BY_HOLE = 0x60;  // 18 signed bytes (from MENU)
constexpr uint32_t STROKES = 0x72;          // halfword: on this hole
constexpr uint32_t RESULT = 0x76;  // halfword: 1 holed in one, 2 holed, -1 limit (inferred)
constexpr uint32_t BALL_REST_X = 0x78, BALL_REST_Y = 0x7c, BALL_REST_ANGLE = 0x80;  // between turns
constexpr uint32_t PLACED = 0x84;  // byte (from MENU): the first putt's ball has been placed
}  // namespace player
// The input the hole reads straight from the wheel slots (flow.cpp fills the table).
namespace wheel_slot {
constexpr uint32_t FLAGS = 0x0;  // halfword; bit 1 = turned this frame
constexpr uint32_t SIZE = 0x8;
}  // namespace wheel_slot
constexpr uint32_t WHEEL_CLOCKWISE = 0, WHEEL_COUNTER = 1;  // slots (inferred from the aim)
constexpr uint32_t SINE_TABLE = GAME_STATE + 0x8531c;       // 256 signed halfwords
constexpr uint32_t TILE_MAP = MENU + 0xb4;                  // -> 256×256 tile bytes of the hole
constexpr uint32_t TILE_MAP_STORE = 0x180d1a00;             // where those bytes live
constexpr uint32_t COURSE_DATA = MENU + 0xb8;               // -> the course's data resource
constexpr uint32_t COURSE_DATA_SIZE = MENU + 0xbc;
constexpr uint32_t HOLE_DATA = GAME_STATE + 0x88038;    // the hole's data resource (0x1800 bytes)
constexpr uint32_t HOLE_LAYOUT = GAME_STATE + 0x89838;  // the hole's object layout (0x2000 bytes)
constexpr uint32_t GROUND_TILE_SCRATCH =
    GAME_STATE + 0x2c;  // an 80×60 tile of ground when one is missing
constexpr uint32_t GROUND_IMAGE = GAME_STATE + 0x8b8fc;  // the ground texture's image
constexpr uint32_t RESULT_TEXT = MENU + 0xb0;            // the end-of-course message; -1 none
constexpr uint32_t HOLE_OBJECTS =
    GAME_STATE + 0x8b000;  // the hole's 3-D objects (0x864 bytes each)
namespace hole_objects {
constexpr uint32_t COUNT = 0x838, CURSOR = 0x840, TABLE = 0x940;
constexpr uint32_t OBJECT_WORDS = 0x219;
}  // namespace hole_objects
// The options, as shown on the Options screen (menu.cpp); saved with the game.
constexpr uint32_t OPTIONS = GAME_STATE + 0x83000;
namespace options {
constexpr uint32_t PLAYER_GENDER = 0xa;  // signed byte: 0 random, 1 female, 2 male
}  // namespace options
// Set when an option changes; the Options screen saves on the way out.
constexpr uint32_t OPTIONS_CHANGED = 0x1803'79ec;
constexpr uint32_t LANGUAGE_WIDE = 10;               // the one language whose text is UTF-16
constexpr uint32_t ALPHABET = GAME_STATE + 0x8300e;  // the name-entry alphabet (resource 0x29)
constexpr uint32_t SCRATCH_TEXT = ALPHABET;  // the same bytes: the menus' text scratch buffer
constexpr uint32_t GLYPH_CODES = GAME_STATE + 0x8400e;  // the code each wheel position enters
constexpr uint32_t GLYPH_BACKSPACE = 0x3a, GLYPH_CONFIRM = 0x3b;
constexpr uint32_t EDIT_BUFFER = GAME_STATE + 0x8f79c;      // the name being entered
constexpr uint32_t NAME_ENTRY_TEXT = GAME_STATE + 0x8380e;  // resource 0x76, the prompt
constexpr uint32_t DIALOG_TEXT = GAME_STATE + 0x8400e;      // the dialog's message resource
constexpr uint32_t DIALOG_MESSAGE = GAME_STATE + 0x8380e;   // the assembled message
constexpr uint32_t TEXT_LAYOUT_OUT = GAME_STATE + 0x84810;  // where text layout writes its lines
// The course loader's scratch (inferred): +1 a byte cleared when a panel message shows, +8 ->
// the buffer the hole's files are read into.
constexpr uint32_t COURSE_LOADER = 0x18034084;
constexpr uint32_t TEXT_LAYOUT_LINES = 0x80, TEXT_LAYOUT_WIDTH = 0x118;  // line slots; pixels
// The live menu item table was GAME_STATE + 0x82bd0 (0x180c4188), and the main menu without
// its "resume" row started one item later; both are host state now (state.h, menu.cpp).
constexpr uint32_t MAIN_MENU_ITEMS = 0x1801'9bf4, MAIN_MENU_ITEMS_SIZE = 0xa8;
constexpr uint32_t DIALOG_ITEMS = 0x1801'9b94, DIALOG_ITEMS_SIZE = 0x30;
constexpr uint32_t MENU_SLIDE_STEP = 0x1e0000;  // 30 pixels a frame, 16.16
constexpr uint32_t MENU_SLIDE_FROM = 0x3a0000;  // items start 58 pixels out
constexpr uint32_t TITLE_FADE_FRAMES = 0x1d, TITLE_BOUNCE_FRAMES = 0x5a, TITLE_EXIT_FRAMES = 0x1e;
constexpr uint32_t PANEL_SCALE_STEP_VALUE = 0x199a;

// The random number generator's state: a 624-word Mersenne Twister table and its index
// (random.cpp).

// What the firmware's state byte asks of the game each tick (inferred from game_tick).
namespace firmware_state {
constexpr uint32_t INITIALISE = 0;
constexpr uint32_t RUN = 1;
constexpr uint32_t SUSPEND_FIRST = 3;  // 3, 4 and 5 all suspend
constexpr uint32_t SUSPEND_LAST = 5;
constexpr uint32_t SUSPENDED = 6;
constexpr uint32_t ANSWER_RUNNING = 1;
constexpr uint32_t ANSWER_UNKNOWN_STATE = 4;
constexpr uint32_t ANSWER_SUSPENDED = 6;
}  // namespace firmware_state

// The random seed the clock is mixed with on the first tick (0x80d85099 in the image).
constexpr uint32_t RANDOM_SEED_MIX = 0x80d8'5099;
constexpr uint32_t MODE_CHANGED_FLAG =
    0x1801'a9bc;  // set to 2 alongside the pipeline start (inferred)

// --- Firmware event / file-request objects (input.cpp) --------------------------------------
// The nodes on the context's lists are the same objects AsyncFileIO works on
// (reversing/asyncfileio-abi.md): a type and state byte at the front, a link, and a completion
// callback with its argument at +0x34/+0x38.
namespace request {
constexpr uint32_t STATE = 0x01;  // byte; 1 = released, 2 = pressed
constexpr uint32_t NEXT = 0x08;
constexpr uint32_t IN_USE = 0x1c;  // byte
constexpr uint32_t RESULT = 0x28;  // -1 when none; otherwise handed to the next node (inferred)
constexpr uint32_t CALLBACK = 0x34;
constexpr uint32_t CONTEXT = 0x38;
}  // namespace request

}  // namespace minigolf::game
