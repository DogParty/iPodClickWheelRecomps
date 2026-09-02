#!/bin/sh
# Regression test: the pause menu opens and the game keeps running.
#
#   tests/pause-menu.sh [path/to/minigolf-headless]
#
# Pressing Menu several minutes into a round used to freeze the game outright. The game works out
# how long a button has been held from when it saw it go down, which it learns from the firmware's
# list of button events; the host had no such list, so every press read as a hold minutes long and
# the game asked to be suspended (game/app.cpp `handle_long_presses`). The screen then stopped.
#
# The oracle cannot catch this: its recordings were made with the emulator, which takes the same
# shortcut, so they contain the freeze too and diff.sh runs with --emulator-firmware to reproduce it.
# This test therefore checks the thing itself — that the picture is still changing after the pause
# menu opens — rather than comparing against a recording.
#
# Exit 2 means "cannot run", which CTest treats as a skip: the game image is the player's own copy.
set -eu

binary=${1:-build/minigolf-headless}
here=$(cd "$(dirname "$0")/.." && pwd)
script="$here/tests/scripts/hole.script"
: "${GAME_DIR:=$here/../../../20 iPod games/Games_RO/88888}"
if [ ! -f "$binary" ] || [ ! -f "$script" ] || [ ! -f "$GAME_DIR/Executables/Minigolf_1_1_2563296.bin" ]; then
    echo "pause-menu.sh: missing the game or the build (set GAME_DIR to your copy)" >&2
    exit 2
fi

copy="$here/build/game-pause-menu"
if [ ! -f "$copy/Executables/Minigolf_1_1_2563296.bin" ]; then
    rm -rf "$copy"
    cp -R "$GAME_DIR" "$copy"
fi
find "$copy" -name '*.sav' -delete

# hole.script presses Menu at frame 9200 and takes screenshots at 9600 and 10200 — after the pause
# menu has opened, and again after Select has resumed the hole.
log="$here/build/pause-menu.stdout"
"$binary" "$copy/Executables/Minigolf_1_1_2563296.bin" --gamedir="$copy" --script="$script" \
    --frames=10400 --time=07:53 > "$log" 2>&1 ||
    { echo "pause-menu.sh: the game exited with status $? (see $log)" >&2; exit 1; }

paused=$(sed -n 's/^screenshot frame 9600 .*fnv1a \([0-9a-f]*\)/\1/p' "$log")
resumed=$(sed -n 's/^screenshot frame 10200 .*fnv1a \([0-9a-f]*\)/\1/p' "$log")
if [ -z "$paused" ] || [ -z "$resumed" ]; then
    echo "pause-menu.sh: the run did not reach both screenshots (see $log)" >&2
    exit 1
fi
if [ "$paused" = "$resumed" ]; then
    echo "pause-menu.sh: the picture never changed after the pause menu opened ($paused twice)." >&2
    echo "pause-menu.sh: the game is frozen — see the note at the top of this script." >&2
    exit 1
fi
echo "pause-menu.sh: the game is still running after the pause menu ($paused -> $resumed)"
