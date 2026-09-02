#!/bin/sh
# Regression test: "Return to Menu" opens the save-game card instead of killing the game.
#
#   tests/return-to-menu.sh [path/to/minigolf-headless]
#
# That row opens the card that asks whether to save first, and the card's renderer (0x18014300)
# was a stub that fired the assert trap: no recorded session ever reached it, so it had never been
# decompiled, and choosing the row ended the process. It is decompiled now, and this is the check
# that it stays reached and stays drawn.
#
# The oracle cannot see this either: none of its recordings open the card. What is checked here is
# that the run survives to the end and that the card is a different picture from the pause menu it
# was chosen from.
#
# Exit 2 means "cannot run", which CTest treats as a skip: the game image is the player's own copy.
set -eu

binary=${1:-build/minigolf-headless}
here=$(cd "$(dirname "$0")/.." && pwd)
script="$here/tests/scripts/return-to-menu.script"
: "${GAME_DIR:=$here/../../../20 iPod games/Games_RO/88888}"
if [ ! -f "$binary" ] || [ ! -f "$script" ] || [ ! -f "$GAME_DIR/Executables/Minigolf_1_1_2563296.bin" ]; then
    echo "return-to-menu.sh: missing the game or the build (set GAME_DIR to your copy)" >&2
    exit 2
fi

copy="$here/build/game-return-to-menu"
if [ ! -f "$copy/Executables/Minigolf_1_1_2563296.bin" ]; then
    rm -rf "$copy"
    cp -R "$GAME_DIR" "$copy"
fi
find "$copy" -name '*.sav' -delete

log="$here/build/return-to-menu.stdout"
"$binary" "$copy/Executables/Minigolf_1_1_2563296.bin" --gamedir="$copy" --script="$script" \
    --frames=12800 --time=07:53 > "$log" 2>&1 ||
    { echo "return-to-menu.sh: the game exited with status $? (see $log)" >&2
      echo "return-to-menu.sh: an assert trap here means the card's render is gone again." >&2
      exit 1; }

menu=$(sed -n 's/^screenshot frame 11000 .*fnv1a \([0-9a-f]*\)/\1/p' "$log")
card=$(sed -n 's/^screenshot frame 12600 .*fnv1a \([0-9a-f]*\)/\1/p' "$log")
if [ -z "$menu" ] || [ -z "$card" ]; then
    echo "return-to-menu.sh: the run did not reach both screenshots (see $log)" >&2
    exit 1
fi
if [ "$menu" = "$card" ]; then
    echo "return-to-menu.sh: the card never appeared ($menu twice)." >&2
    exit 1
fi
echo "return-to-menu.sh: the save-game card appeared ($menu -> $card)"
