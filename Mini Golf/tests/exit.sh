#!/bin/sh
# Regression test: Exit on the main menu ends the program.
#
#   tests/exit.sh [path/to/minigolf-headless]
#
# The row asks the firmware to suspend the eApp — on an iPod that hands control back to the iPod's
# own menu. This firmware has nothing to hand back to, so the program ends. It used to run on with
# the game suspended, which looked like a freeze.
#
# The check is that the run stops before the script does: the script takes a screenshot after the
# row is chosen, and that screenshot must never be reached.
#
# Exit 2 means "cannot run", which CTest treats as a skip: the game image is the player's own copy.
set -eu

binary=${1:-build/minigolf-headless}
here=$(cd "$(dirname "$0")/.." && pwd)
script="$here/tests/scripts/exit.script"
: "${GAME_DIR:=$here/../../../20 iPod games/Games_RO/88888}"
if [ ! -f "$binary" ] || [ ! -f "$script" ] || [ ! -f "$GAME_DIR/Executables/Minigolf_1_1_2563296.bin" ]; then
    echo "exit.sh: missing the game or the build (set GAME_DIR to your copy)" >&2
    exit 2
fi

copy="$here/build/game-exit"
if [ ! -f "$copy/Executables/Minigolf_1_1_2563296.bin" ]; then
    rm -rf "$copy"
    cp -R "$GAME_DIR" "$copy"
fi
find "$copy" -name '*.sav' -delete

log="$here/build/exit.stdout"
"$binary" "$copy/Executables/Minigolf_1_1_2563296.bin" --gamedir="$copy" --script="$script" \
    --frames=4000 --time=07:53 > "$log" 2>&1 ||
    { echo "exit.sh: the game exited with status $? (see $log)" >&2; exit 1; }

if ! grep -q "screenshot frame 2400" "$log"; then
    echo "exit.sh: the run never reached the main menu (see $log)" >&2
    exit 1
fi
if grep -q "screenshot frame 3200" "$log"; then
    echo "exit.sh: the game was still running after Exit was chosen." >&2
    exit 1
fi
echo "exit.sh: Exit ended the program"
