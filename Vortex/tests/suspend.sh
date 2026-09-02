#!/bin/sh
# The game asking to be put away, and the program answering.
#
#   tests/suspend.sh [path/to/vortex-headless]
#
# Menu on the main menu is this game's way out. It writes 6 — "suspended" — into the byte at
# `ctx+0` and from that frame on makes four framework calls a frame and draws nothing, waiting
# for a firmware that will never come back (PLAN.md progress log; runtime/main.cpp
# CONTEXT_STATE_SUSPENDED). The pump watches for it and ends the run, which is what "Escape quits
# the game" means here; without that the window sits on a dead frame and looks frozen, which is
# what it did.
#
# No recording can guard this: the emulator does *not* stop on a suspended game — it kept calling
# one to the end of the script, and every recording in tests/expected/ is of that — so the check
# is on the recomp alone, run the way a player runs it. The script presses Menu on the main menu
# at frame 4400 and would otherwise run to 5800; passing means the run ended within a few frames
# of the press, and that it took the menu path rather than dying on the way.
set -eu

binary=${1:-}
here=$(cd "$(dirname "$0")/.." && pwd)
. "$here/tests/game-dir.sh"

[ -n "$binary" ] || binary="$here/build/vortex-headless"
script="$here/analysis/scripts/menu-probe.script"
[ -f "$script" ] || { echo "suspend.sh: no $script" >&2; exit 2; }
[ -x "$binary" ] || { echo "suspend.sh: no $binary" >&2; exit 2; }

game_dir=$(game_dir_for "suspend") || exit 2
log="$here/build/suspend.calls"
rm -f "$log"

# A real run: no --emulator-firmware, because that is the flag that reproduces the emulator's
# habit of calling a suspended game for ever.
"$binary" "$game_dir/$GAME_IMAGE_PATH" --gamedir="$game_dir" --script="$script" \
    --frames=5801 --time=00:00 --call-log="$log" > "$here/build/suspend.stdout" 2>&1 \
    || { echo "suspend.sh: the recomp exited with status $? (see build/suspend.stdout)" >&2; exit 1; }

last=$(tail -1 "$log" | awk '{print $1}')
press=4400
[ -n "$last" ] || { echo "suspend.sh: the run logged no framework calls at all" >&2; exit 1; }

# It must have reached the menu — a run that fell over earlier would also "stop early".
if [ "$last" -lt "$press" ]; then
    echo "suspend.sh: the run ended at frame $last, before the Menu press at $press: it did not" \
         "reach the main menu, so this proves nothing about the suspend" >&2
    exit 1
fi
# ...and it must have stopped promptly after it, rather than running on to the script's own quit.
if [ "$last" -gt $(( press + 20 )) ]; then
    echo "suspend.sh: the run went on to frame $last after the game asked to be suspended at" \
         "$press — the pump is not answering (runtime/main.cpp CONTEXT_STATE_SUSPENDED)" >&2
    exit 1
fi

printf 'suspend.sh: the game asked to be put away at frame %s and the run ended at %s\n' \
    "$press" "$last"
