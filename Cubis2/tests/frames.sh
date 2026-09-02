#!/bin/sh
# The picture oracle: run one script through both the recomp and the pinned emulator and compare
# the frames they drew.
#
#   tests/frames.sh <case> [path/to/cubis-headless]
#
# The call-log oracle (tests/diff.sh) cannot see this. A draw hands the framework an address and
# a count; what came out of that address is never an argument, so a texture decoded through the
# wrong palette, a quad in the wrong place or a colour key left in makes no difference to the log
# at all. Every title before this one found faults here that the call log had passed to the last
# word, and this title's first finding was one: the emulator rendered every cube on its board grey
# because it ignored the constant colour register, and no call log on either side could have
# noticed (PLAN.md difference 6). The register is the whole reason this case matters here — the
# board, the window frame and the battery gauge are all painted through it.
#
# Both sides must agree about it, which means the pinned emulator carries the fix
# (reference/MANIFEST.md) and `../common/src/ipod/libeapp/gles.cpp` carries the same rule. A
# comparison across a build where one of them does not is not a comparison of two renderers; it
# is a comparison of two rules.
#
# Nothing is stored: both sides are run now. Screenshots of the game are the owner's art and do
# not belong in this repository, and a stored reference would in any case only be as good as the
# day it was taken. The script says where the shots go; a case with no `shot` action is skipped.
#
# The emulator writes every screenshot to /tmp/ipod-shot-NN.png — one path, shared by every
# title's picture oracle on this machine — and another run can add to, overwrite or sweep that
# directory within a second (PLAN.md rule 11). So the emulator's output is read line by line and
# each screenshot is copied the moment its line appears, under the frame number that line names;
# a shot that was gone by then, or a shot the script did not ask for, fails the case rather than
# being compared.
set -eu

case_name=${1:?usage: tests/frames.sh <case> [cubis-headless]}
binary=${2:-}
here=$(cd "$(dirname "$0")/.." && pwd)
. "$here/tests/game-dir.sh"

script="$here/tests/scripts/$case_name.script"
[ -f "$script" ] || { echo "frames.sh: no $script" >&2; exit 2; }
grep -q '^[0-9][0-9]*: *shot' "$script" || {
    echo "frames.sh: $case_name takes no screenshots — nothing to compare" >&2
    exit 2
}

[ -n "$binary" ] || binary="$here/build/cubis-headless"
emulator="$here/build/oracle-emulator/release/play"
[ -x "$emulator" ] || {
    echo "frames.sh: build the pinned emulator first (see reference/MANIFEST.md)" >&2
    exit 2
}

frames_dir="$here/build/frames-$case_name"
rm -rf "$frames_dir"
mkdir -p "$frames_dir/emulator" "$frames_dir/recomp"

# The emulator runs the frame on which the script says `quit`, so the recomp is given one more.
quit_frame=$(sed -n 's/^\([0-9][0-9]*\): *quit.*/\1/p' "$script" | head -1)
frames=$(( ${quit_frame:-0} + 1 ))

# The screenshots this case asks for, by frame, in order. The emulator numbers its shots from 01
# in the order it takes them, and so does the recomp, so the Nth shot of each is the same frame
# — provided every one of the emulator's arrived, which is what the copy loop below checks.
wanted_frames=$(sed -n 's/^\([0-9][0-9]*\): *shot.*/\1/p' "$script")
wanted_shots=$(printf '%s\n' "$wanted_frames" | grep -c .)

game_dir=$(game_dir_for "frames-emulator-$case_name") || exit 2
# The same flags as tests/record.sh, for the same reasons; the picture is not compared against a
# recording, but the run must take the recorded path.
"$emulator" "$game_dir/$GAME_IMAGE_PATH" --gamedir="$game_dir" \
    --load-on-open --ctx-seed=5 --allow-creates --fixed-clock --fps=0 --time=00:00 --script="$script" 2>&1 \
| while IFS= read -r line; do
    printf '%s\n' "$line"
    case $line in
        "screenshot -> "*)
            # `screenshot -> /tmp/ipod-shot-01.png  (frame 900)`
            path=${line#screenshot -> }
            path=${path%%  *}
            number=${path##*/ipod-shot-}
            number=${number%.png}
            frame=${line##*(frame }
            frame=${frame%)}
            cp "$path" "$frames_dir/emulator/ipod-shot-$number.png" 2>/dev/null \
                || printf 'frames.sh: the screenshot at frame %s was gone before it could be copied\n' "$frame"
            ;;
    esac
done > "$frames_dir/emulator.stdout"

if grep -q '^button .*-> flags bit' "$frames_dir/emulator.stdout"; then
    echo "frames.sh: $case_name: the emulator's window received a press the script did not make" >&2
    exit 1
fi
got_frames=$(sed -n 's/^screenshot -> .*(frame \([0-9]*\))$/\1/p' "$frames_dir/emulator.stdout" | tr '\n' ' ')
if [ "$(printf '%s\n' "$wanted_frames" | tr '\n' ' ')" != "$got_frames" ]; then
    echo "frames.sh: the emulator took screenshots at frames [$got_frames] for a case that asks" \
         "for [$(printf '%s\n' "$wanted_frames" | tr '\n' ' ')]" >&2
    exit 1
fi
got_shots=$(ls "$frames_dir/emulator"/ipod-shot-*.png 2>/dev/null | wc -l | tr -d ' ')
if [ "$got_shots" != "$wanted_shots" ]; then
    echo "frames.sh: only $got_shots of the emulator's $wanted_shots screenshot(s) could be kept —" \
         "another run was sweeping /tmp at the same time; run this case again" >&2
    exit 1
fi

game_dir=$(game_dir_for "frames-recomp-$case_name") || exit 2
# --emulator-graphics renders as the emulator does where the two deliberately differ (the
# minified-texture filter; framework/graphics.h), so that a difference here is a fault and not a
# design decision.
CUBIS_SHOT_DIR="$frames_dir/recomp" "$binary" "$game_dir/$GAME_IMAGE_PATH" \
    --gamedir="$game_dir" --script="$script" --frames="$frames" --time=00:00 --emulator-firmware \
    --emulator-graphics \
    > "$frames_dir/recomp.stdout" 2>&1

status=0
python3 "$here/tests/frames.py" "$frames_dir/emulator" "$frames_dir/recomp" || status=$?
exit $status
