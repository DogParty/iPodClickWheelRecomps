// See cheats.h.
#include "game/cheats.h"

#include "platform/settings.h"
#include "runtime/memory.h"

namespace lost::game {

namespace {

// --- what a menu item is ----------------------------------------------------------------------
//
// Every menu in this game is an array of 32-bit words in the image's own data, one per item, and
// the game rewrites them where it stands rather than building a copy: the low halfword is the
// item's index into the string table (the file `s`), and the high halfword is its flags. Both
// halves were read off a running game — `--dump-frame=180405dc:20` at the PLAY submenu prints
//
//     00000002 00020003 0000008b 0000004c 00000049
//
// which is START NEW GAME (string 2), CONTINUE GAME (3), SELECT CHAPTER (0x8b), then BACK and
// SELECT — and the image's own bytes at that address are `00020002 00020003 ...`, so the game
// had cleared bit 0x20000 on the first and left it on the second. On that screen START NEW GAME
// is drawn and CONTINUE GAME is not, which is what says the bit means *hidden*.
//
// The code that does it is at 0x18036118: it compares the menu it is setting up against the
// address of each table it knows, and for this one looks up the items for strings 2 and 3 and
// clears the bit on one and sets it on the other. The counting loop just below at 0x18036234
// walks the same array and counts the items without 0x20000 — how many the menu has — and among
// those the ones without 0x80000, which is the second flag and greys an item rather than
// removing it. Only the first is used to lock a chapter.
constexpr uint32_t MENU_ITEM_HIDDEN = 0x0002'0000;
constexpr uint32_t MENU_ITEM_STRING = 0x0000'ffff;

// --- the chapter menu ---------------------------------------------------------------------
//
// The nine chapters, as one menu table of eleven items at 0x18040530. The image ships it with
// every flag word clear:
//
//     8c 00 00 00  8d 00 00 00 ... 94 00 00 00  48 00 00 00  49 00 00 00
//
// which is strings 0x8c..0x94 — "The Arrival", "First Taste", "Survivors", "Old Medicine",
// "Kidnapped", "The Black Rock", "A Matter of Numbers", "The Smoke Monster", "The Escape" — and
// then BACK and SELECT. The count of eleven is the code's own: 0x180367d8, the arm of the menu
// dispatch that runs when SELECT CHAPTER is chosen, loads this address and `mov r2, #11`.
//
// On a profile that has finished nothing, the same dump at the chapter screen reads
//
//     0000008c 0002008d 0002008e 0002008f 00020090 00020091 00020092 00020093 00020094
//     00000048 00000049
//
// — chapter 1 available and the other eight hidden. **That is the whole of the lock**, and
// clearing that one bit on those eight words is the whole of this cheat.
constexpr uint32_t CHAPTER_MENU = 0x1804'0530;
constexpr unsigned CHAPTER_COUNT = 9;
constexpr uint32_t FIRST_CHAPTER_STRING = 0x8c;  // "The Arrival"

// Is the table still the one described above?
//
// The addresses here belong to one build of one game — `Lost_1_1_2917525.bin`, which is the
// build this project is a recompilation of and the only one it will load. The check costs nine
// loads a frame and means that if it is ever pointed at something else, the cheat does nothing
// instead of writing eight words into the middle of whatever is there.
bool chapter_menu_is_where_it_should_be() {
    for (unsigned chapter = 0; chapter < CHAPTER_COUNT; ++chapter) {
        const uint32_t item = ld32(CHAPTER_MENU + chapter * 4);
        if ((item & MENU_ITEM_STRING) != FIRST_CHAPTER_STRING + chapter) {
            return false;
        }
    }
    return true;
}

// Show every chapter in the chapter menu.
//
// Every frame, because the game re-hides them: the flags are set when the menu is built, so
// clearing them once would last exactly until the player backed out of the screen and opened it
// again. Nine loads and at most eight stores is cheap enough to simply not care.
//
// What this does *not* do is tell the game a chapter has been finished. Nothing about the
// player's progress is touched, the save is not written, and turning the switch off puts the
// menu back the way the game wants it on the very next frame.
void unlock_all_chapters() {
    if (!chapter_menu_is_where_it_should_be()) {
        return;
    }
    for (unsigned chapter = 0; chapter < CHAPTER_COUNT; ++chapter) {
        const uint32_t address = CHAPTER_MENU + chapter * 4;
        const uint32_t item = ld32(address);
        if ((item & MENU_ITEM_HIDDEN) != 0) {
            st32(address, item & ~MENU_ITEM_HIDDEN);
        }
    }
}

}  // namespace

bool any_cheat_enabled() {
    return platform::settings().unlock_all_chapters;
}

void apply_cheats() {
    if (platform::settings().unlock_all_chapters) {
        unlock_all_chapters();
    }
}

}  // namespace lost::game
