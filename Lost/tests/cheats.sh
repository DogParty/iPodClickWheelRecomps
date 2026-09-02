#!/bin/sh
# Unlock all chapters: the cheat shows the eight chapters the game is hiding, and only then.
#
#   tests/cheats.sh [path/to/lost-headless]
#
# Neither of the other two oracles can see this and neither should: a cheat is this project's own
# behaviour and there is nothing in the emulator to compare it with. What is worth pinning is not
# the picture but **the address** — src/game/cheats.cpp names one word in the image for each of
# the nine chapters, and those nine words are the whole of what this feature knows. If the menu
# ever moves, or the flag ever stops meaning "hidden", this is what says so; without it the cheat
# would go on writing into whatever had taken that address and the only symptom would be a
# stranger's bug report.
#
# So the run below opens Play ▸ Select Chapter twice, with the cheat and without, and reads the
# eleven words of the chapter menu straight out of guest memory:
#
#   locked    0000008c 0002008d 0002008e ... 00020094 00000048 00000049
#   unlocked  0000008c 0000008d 0000008e ... 00000094 00000048 00000049
#
# The low halfword is the item's string index — 0x8c "The Arrival" through 0x94 "The Escape" —
# and bit 0x20000 of the high halfword is the game's own "hidden". Both lines are checked: that
# the game really does hide eight of them is as much a part of this as that the cheat shows them,
# because a cheat that changed nothing would pass a test that only looked at the unlocked run.
set -eu

binary=${1:-}
here=$(cd "$(dirname "$0")/.." && pwd)
. "$here/tests/game-dir.sh"

[ -n "$binary" ] || binary="$here/build/lost-headless"
[ -x "$binary" ] || { echo "cheats.sh: no $binary" >&2; exit 2; }

script="$here/tests/scripts/chapter-select.script"
[ -f "$script" ] || { echo "cheats.sh: no $script" >&2; exit 2; }
last_frame=$(sed -n 's/^\([0-9][0-9]*\): *quit.*/\1/p' "$script" | head -1)

# The chapter menu, as eleven words at 0x18040530, printed on the last frame of the run — by
# which time the script has walked to SELECT CHAPTER and opened it.
CHAPTER_MENU=18040530
dump="$CHAPTER_MENU:2c:$(printf '%x' "$((last_frame - 2))")"

# The saved game the script starts from: a profile with nothing finished, which is the state the
# game hides eight chapters in. `tests/game-dir.sh` gives each run its own copy, so neither of the
# two runs below can see what the other did.
run_and_read_menu() {  # run_and_read_menu <case> [extra flags...]
    case_name=$1
    shift
    game_dir=$(game_dir_for "$case_name") || exit 2
    "$binary" "$game_dir/$GAME_IMAGE_PATH" --gamedir="$game_dir" --script="$script" \
        --frames=$((last_frame + 1)) --dump-frame="$dump" "$@" \
        > "$here/build/$case_name.log" 2>&1
    # The dump is two indented lines under `frame N at ...`; the words themselves are what is
    # wanted, on one line, in order.
    sed -n '/^frame [0-9]* at '"$CHAPTER_MENU"':$/{n;p;n;p;}' "$here/build/$case_name.log" |
        tail -2 | tr -s ' \n' ' ' | sed 's/^ //; s/ $//'
}

locked=$(run_and_read_menu cheats-locked)
unlocked=$(run_and_read_menu cheats-unlocked --cheats=unlock-chapters)

expected_locked="0000008c 0002008d 0002008e 0002008f 00020090 00020091 00020092 00020093 00020094 00000048 00000049"
expected_unlocked="0000008c 0000008d 0000008e 0000008f 00000090 00000091 00000092 00000093 00000094 00000048 00000049"

status=0
if [ "$locked" = "$expected_locked" ]; then
    echo "without the cheat: chapter 1, and eight hidden"
else
    echo "cheats.sh: the chapter menu is not where or what src/game/cheats.cpp says." >&2
    echo "  expected  $expected_locked" >&2
    echo "  found     $locked" >&2
    status=1
fi

if [ "$unlocked" = "$expected_unlocked" ]; then
    echo "with the cheat:    all nine chapters"
else
    echo "cheats.sh: the cheat did not unlock every chapter." >&2
    echo "  expected  $expected_unlocked" >&2
    echo "  found     $unlocked" >&2
    status=1
fi

exit $status
