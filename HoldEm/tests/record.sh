#!/bin/sh
# Record an oracle case: run a script through the pinned emulator and keep the framework-call log.
#
#   tests/record.sh <case>
#
# <case> names tests/scripts/<case>.script; the log goes to tests/expected/<case>.calls. The
# recording is the case's ground truth, so the flags below are part of the case and not a matter
# of taste — they are what tests/diff.sh reproduces on the recomp's side:
#
#   --load-on-open   an open of this game's is a load: it hands `#3` a buffer and expects the
#                    bytes to arrive with the completion (reference/eapp-loader/play.rs,
#                    `defaults_for`, and PLAN.md difference 2). Off by default in the emulator.
#   (async files)    ...and the completion is delivered between frames, not from inside the
#                    open. Against a synchronous open the game never reads `Data/textures.txt`
#                    and walks off the end of its texture table. This is the emulator's default
#                    for every title (`--sync-files` is the flag that turns it off), so there is
#                    nothing to pass — noted because the recomp's file layer has to do the same.
#   --ctx-seed=0     the one-time init call must see reason 0, or the game never registers its
#                    context and re-runs its boot every frame (PLAN.md difference 1).
#   --frame-reason=1 every frame after it is an ordinary frame.
#   --allow-creates  the game writes `en.lproj/data.txt`; without this the write fails and it
#                    takes a different path.
#   --fixed-clock    the µs clock advances a fixed step per call rather than following the wall,
#                    so two runs of the same script see the same time pass.
#   --fps=0          no frame limiter: a recording should not depend on how fast this machine is.
#
# The emulator's own defaults table gives a binary named `HoldEm*` the first three of these
# already; they are spelled out so that the case does not depend on a table in another tree.
#
# The emulator used is the pinned copy under tools/oracle-emulator (reference/MANIFEST.md), never
# the live tree, so a recording stays reproducible while the emulator moves on.
set -eu

case_name=${1:?usage: tests/record.sh <case>}
here=$(cd "$(dirname "$0")/.." && pwd)
. "$here/tests/game-dir.sh"

script="$here/tests/scripts/$case_name.script"
expected="$here/tests/expected/$case_name.calls"
emulator="$here/build/oracle-emulator/release/play"

if [ ! -f "$script" ]; then
    echo "record.sh: no $script" >&2
    exit 1
fi
if [ ! -x "$emulator" ]; then
    echo "record.sh: build the pinned emulator first:" >&2
    echo "  cargo build --release --manifest-path tools/oracle-emulator/Cargo.toml \\" >&2
    echo "      --target-dir build/oracle-emulator" >&2
    exit 1
fi

game_dir=$(game_dir_for "record-$case_name") || exit 2
mkdir -p "$here/tests/expected"

"$emulator" "$game_dir/$GAME_IMAGE_PATH" --gamedir="$game_dir" \
    --load-on-open --ctx-seed=0 --frame-reason=1 \
    --allow-creates --fixed-clock --fps=0 \
    --script="$script" --call-log="$expected" \
    > "$here/build/record-$case_name.stdout" 2>&1

printf 'recorded %s: %s calls\n' "$case_name" "$(wc -l < "$expected" | tr -d ' ')"
