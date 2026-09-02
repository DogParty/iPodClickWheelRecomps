#!/bin/sh
# Regression test: the Cheats screen this port adds can be reached, used, and left again, and
# what it was told is on file afterwards.
#
#   tests/cheats.sh [path/to/minigolf-headless]
#
# The oracle cannot check any of this — it compares against logs recorded from the emulator,
# which had no Cheats row at all, and `--emulator-firmware` takes the row away again so those
# recordings still line up (src/game/cheats.h). So the feature needs a test of its own, and this
# is it: the screen is opened, two cheats are turned on, and Menu goes back to Options.
#
# Three things are checked, each of which has broken at some point while this was written:
# that the screen is reached (the picture changes when Select is pressed on the Cheats row),
# that toggling redraws the row it toggled (the label carries its own ON/OFF), and that the
# flags reached cheats.txt in the platform's save store.
#
# The pictures are compared for *difference* only. Two frames of the same screen never hash the
# same anyway — the ball beside the cursor spins and the selected row's letters ripple — so
# "came back to the same screen" is not a thing a hash can say.
#
# Exit 2 means "cannot run", which CTest treats as a skip: the game image is the player's own copy.
set -eu

binary=${1:-build/minigolf-headless}
here=$(cd "$(dirname "$0")/.." && pwd)
script="$here/tests/scripts/cheats.script"
: "${GAME_DIR:=$here/../../../20 iPod games/Games_RO/88888}"
if [ ! -f "$binary" ] || [ ! -f "$script" ] || [ ! -f "$GAME_DIR/Executables/Minigolf_1_1_2563296.bin" ]; then
    echo "cheats.sh: missing the game or the build (set GAME_DIR to your copy)" >&2
    exit 2
fi

# A fresh copy of the game directory: the cheats file is written beside the game, and a run must
# not be able to pass because an earlier one left the flags set.
copy="$here/build/game-cheats"
if [ ! -f "$copy/Executables/Minigolf_1_1_2563296.bin" ]; then
    rm -rf "$copy"
    cp -R "$GAME_DIR" "$copy"
fi
find "$copy" -name '*.sav' -delete
rm -f "$copy/cheats.txt"

log="$here/build/cheats.stdout"
"$binary" "$copy/Executables/Minigolf_1_1_2563296.bin" --gamedir="$copy" --script="$script" \
    --frames=6200 --time=07:53 > "$log" 2>&1 ||
    { echo "cheats.sh: the game exited with status $? (see $log)" >&2; exit 1; }

shot() {
    sed -n "s/^screenshot frame $1 .*fnv1a \([0-9a-f]*\)/\1/p" "$log"
}
options=$(shot 2900)     # Options, cursor on the Cheats row
opened=$(shot 4100)      # the Cheats screen
toggled=$(shot 4400)     # after Select on the first row
second=$(shot 5300)      # after Select on the second
back=$(shot 6000)        # Options again
for frame in "$options" "$opened" "$toggled" "$second" "$back"; do
    if [ -z "$frame" ]; then
        echo "cheats.sh: the run did not reach every screenshot (see $log)" >&2
        exit 1
    fi
done
if [ "$options" = "$opened" ]; then
    echo "cheats.sh: Select on the Cheats row did not open the screen ($options twice)" >&2
    exit 1
fi
if [ "$opened" = "$toggled" ]; then
    echo "cheats.sh: toggling a cheat did not redraw its row ($opened twice)" >&2
    exit 1
fi
if [ "$back" = "$second" ]; then
    echo "cheats.sh: Menu did not leave the Cheats screen ($second twice)" >&2
    exit 1
fi

saved="$copy/cheats.txt"
if [ ! -f "$saved" ]; then
    echo "cheats.sh: no cheats.txt was written" >&2
    exit 1
fi
for expected in "unlock-courses 1" "no-stroke-limit 1" "aim-guide 0"; do
    if ! grep -qx "$expected" "$saved"; then
        echo "cheats.sh: \"$expected\" is not in $saved:" >&2
        cat "$saved" >&2
        exit 1
    fi
done
echo "cheats.sh: the screen works and the flags are on file ($options -> $opened -> $toggled)"
