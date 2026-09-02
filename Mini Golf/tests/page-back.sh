#!/bin/sh
# Regression test: a page (Statistics, Help, Volume...) can be left again.
#
#   tests/page-back.sh [path/to/minigolf-headless]
#
# Leaving a page means calling the screen's `next_enter`, which `page_enter` works out and used to
# store before installing the screen — and installing a screen writes all four of its fields, so
# the stored value was overwritten with nothing. Select and Menu then did nothing at all and the
# page could not be left: the game looked frozen.
#
# The oracle did not catch it: no recorded case opens a page and leaves it again. This checks the
# thing itself — that the picture changes after Menu is pressed on a page.
#
# Exit 2 means "cannot run", which CTest treats as a skip: the game image is the player's own copy.
set -eu

binary=${1:-build/minigolf-headless}
here=$(cd "$(dirname "$0")/.." && pwd)
script="$here/tests/scripts/page-back.script"
: "${GAME_DIR:=$here/../../../20 iPod games/Games_RO/88888}"
if [ ! -f "$binary" ] || [ ! -f "$script" ] || [ ! -f "$GAME_DIR/Executables/Minigolf_1_1_2563296.bin" ]; then
    echo "page-back.sh: missing the game or the build (set GAME_DIR to your copy)" >&2
    exit 2
fi

copy="$here/build/game-page-back"
if [ ! -f "$copy/Executables/Minigolf_1_1_2563296.bin" ]; then
    rm -rf "$copy"
    cp -R "$GAME_DIR" "$copy"
fi
find "$copy" -name '*.sav' -delete

# page-back.script opens the Statistics page, screenshots it at 3000, presses Menu at 3200, and
# screenshots again at 3800 and 4400.
log="$here/build/page-back.stdout"
"$binary" "$copy/Executables/Minigolf_1_1_2563296.bin" --gamedir="$copy" --script="$script" \
    --frames=4600 --time=07:53 > "$log" 2>&1 ||
    { echo "page-back.sh: the game exited with status $? (see $log)" >&2; exit 1; }

paused=$(sed -n 's/^screenshot frame 3000 .*fnv1a \([0-9a-f]*\)/\1/p' "$log")
resumed=$(sed -n 's/^screenshot frame 4400 .*fnv1a \([0-9a-f]*\)/\1/p' "$log")
if [ -z "$paused" ] || [ -z "$resumed" ]; then
    echo "page-back.sh: the run did not reach both screenshots (see $log)" >&2
    exit 1
fi
if [ "$paused" = "$resumed" ]; then
    echo "page-back.sh: Menu did not leave the page ($paused twice)." >&2
    echo "page-back.sh: see the note at the top of this script." >&2
    exit 1
fi
echo "page-back.sh: the page was left ($paused -> $resumed)"
