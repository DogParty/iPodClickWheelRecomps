#!/bin/sh
# Save and Exit: the game writes its save, asks to be put away, and the program ends.
#
#   tests/save-and-exit.sh [path/to/lost-headless]
#
# Neither of the other two oracles can see this. The call-log oracle compares against a recording
# made with a fixed frame reason, which is the very thing that broke it; the picture oracle
# compares against the emulator, which hangs on this screen for the same reason. So this case is
# checked against what the *game* does rather than against another implementation of the firmware.
#
# What went wrong, and what each check below would have caught:
#
#   * The game answers 5 — "call me back so I can shut down" — in the byte at `ctx+0x100`, and
#     the frame loop used to overwrite that request with its own reason every frame. The save was
#     written and the game then sat on its SAVING screen for ever. `ended_early` is that bug.
#   * A save is written by the store ordinals and committed on close, so a run that ends without
#     ever closing leaves nothing behind. `save written` is that one.
#   * The game opens a save with write mode even when it means to read it, so a boot that
#     truncates loses the save at the moment it is loaded. `survives a boot` is that one.
set -eu

binary=${1:-}
here=$(cd "$(dirname "$0")/.." && pwd)
. "$here/tests/game-dir.sh"

[ -n "$binary" ] || binary="$here/build/lost-headless"
[ -x "$binary" ] || { echo "save-and-exit.sh: no $binary" >&2; exit 2; }

script="$here/tests/scripts/save-and-exit.script"
quit_frame=$(sed -n 's/^\([0-9][0-9]*\): *quit.*/\1/p' "$script" | head -1)

game_dir=$(game_dir_for "save-and-exit") || exit 2
save="$game_dir/lost.sav0"
rm -f "$save"

# --frames is the script's own quit frame. If the game is still running when it arrives, the run
# ends because the limit ran out rather than because the game asked to stop, and `frame N` on the
# last line says which of the two happened.
log="$here/build/save-and-exit.log"
"$binary" "$game_dir/$GAME_IMAGE_PATH" --gamedir="$game_dir" --script="$script" \
    --frames=$((quit_frame + 1)) --dump-frame=190000a0:4 > "$log" 2>&1

status=0
last_frame=$(sed -n 's/^frame \([0-9][0-9]*\) at.*/\1/p' "$log" | tail -1)
if [ -z "$last_frame" ] || [ "$last_frame" -ge "$quit_frame" ]; then
    echo "save-and-exit.sh: the game was still running at frame ${last_frame:-?} — Save and Exit" \
         "never ended it (the SAVING screen hang)" >&2
    status=1
else
    echo "the game put itself away at frame $last_frame"
fi

if [ ! -s "$save" ]; then
    echo "save-and-exit.sh: no save written to $save" >&2
    status=1
else
    echo "save written: $(wc -c < "$save" | tr -d ' ') bytes"
fi

# Boot again and check the save is still the same bytes afterwards. The game opens it during
# start-up, and an open that truncated would leave a shorter or empty file behind.
if [ -s "$save" ]; then
    before=$(shasum -a 256 < "$save")
    boot="$here/build/save-and-exit-boot.script"
    printf '300: quit\n' > "$boot"
    "$binary" "$game_dir/$GAME_IMAGE_PATH" --gamedir="$game_dir" --script="$boot" \
        --frames=301 > "$here/build/save-and-exit-boot.log" 2>&1
    if [ "$before" = "$(shasum -a 256 < "$save")" ]; then
        echo "the save survives a boot unchanged"
    else
        echo "save-and-exit.sh: booting the game changed the save" >&2
        status=1
    fi
fi

exit $status
