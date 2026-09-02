#!/bin/sh
# The picture oracle: run one script through both the recomp and the pinned emulator and compare
# the frames they drew.
#
#   tests/frames.sh <case> [path/to/lost-headless]
#
# The call-log oracle (tests/diff.sh) cannot see this. A draw hands the framework an address and
# a count; what came out of that address is never an argument, so a texture decoded through the
# wrong palette, a quad in the wrong place or a colour key left in makes no difference to the log
# at all. Both faults found on day one were found here and could not have been found there:
# every 320x240 scene background rendered as colour noise, and the game's text rendered soft,
# while the call logs were identical to the last word.
#
# Nothing is stored: both sides are run now. Screenshots of the game are the owner's art and do
# not belong in this repository, and a stored reference would in any case only be as good as the
# day it was taken. The script says where the shots go; a case with no `shot` action is skipped.
set -eu

case_name=${1:?usage: tests/frames.sh <case> [lost-headless]}
binary=${2:-}
here=$(cd "$(dirname "$0")/.." && pwd)
. "$here/tests/game-dir.sh"

script="$here/tests/scripts/$case_name.script"
[ -f "$script" ] || { echo "frames.sh: no $script" >&2; exit 2; }
grep -q '^[0-9][0-9]*: *shot' "$script" || {
    echo "frames.sh: $case_name takes no screenshots — nothing to compare" >&2
    exit 2
}

[ -n "$binary" ] || binary="$here/build/lost-headless"
emulator="$here/build/oracle-emulator/release/play"
[ -x "$emulator" ] || {
    echo "frames.sh: build the pinned emulator first (see reference/MANIFEST.md)" >&2
    exit 2
}

# The emulator writes its screenshots to a fixed path in /tmp that any other run would overwrite,
# so they are moved somewhere this case owns as soon as it is finished.
frames_dir="$here/build/frames-$case_name"
rm -rf "$frames_dir"
mkdir -p "$frames_dir/emulator" "$frames_dir/recomp"

# The emulator runs the frame on which the script says `quit`, so the recomp is given one more.
quit_frame=$(sed -n 's/^\([0-9][0-9]*\): *quit.*/\1/p' "$script" | head -1)
frames=$(( ${quit_frame:-0} + 1 ))

# How many screenshots this case asks for. Checked below, because the emulator writes its
# screenshots to a shared path in /tmp that any other run of it — another case, another window,
# somebody else's — can add to or overwrite. A miscount there would otherwise arrive as a
# comparison between frames that are not the same frames.
wanted_shots=$(grep -c '^[0-9][0-9]*: *shot' "$script")

game_dir=$(game_dir_for "frames-emulator-$case_name") || exit 2
rm -f /tmp/ipod-shot-*.png
"$emulator" "$game_dir/$GAME_IMAGE_PATH" --gamedir="$game_dir" \
    --load-on-open --allow-creates --fixed-clock --fps=0 --script="$script" \
    > "$frames_dir/emulator.stdout" 2>&1
got_shots=$(ls /tmp/ipod-shot-*.png 2>/dev/null | wc -l | tr -d ' ')
if [ "$got_shots" != "$wanted_shots" ]; then
    echo "frames.sh: the emulator left $got_shots screenshot(s) in /tmp for a case that asks for" \
         "$wanted_shots — another run of it was writing there at the same time" >&2
    exit 1
fi
mv /tmp/ipod-shot-*.png "$frames_dir/emulator/"

game_dir=$(game_dir_for "frames-recomp-$case_name") || exit 2
# --emulator-graphics renders the backdrop pipeline the way the emulator does, which is the one
# rendering decision the two make differently on purpose (framework/graphics.h). Without it a
# third of every frame — the letterbox bars and the dialogue panels — would differ by design and
# this comparison would have to be blinded to those rows, which is exactly where a real fault
# would then hide.
LOST_SHOT_DIR="$frames_dir/recomp" "$binary" "$game_dir/$GAME_IMAGE_PATH" \
    --gamedir="$game_dir" --script="$script" --frames="$frames" --time=00:00 --emulator-firmware \
    --emulator-graphics \
    > "$frames_dir/recomp.stdout" 2>&1

status=0
python3 "$here/tests/frames.py" "$frames_dir/emulator" "$frames_dir/recomp" || status=$?
exit $status
