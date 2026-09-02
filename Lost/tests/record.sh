#!/bin/sh
# Record an oracle case: run a script through the pinned emulator and keep the framework-call log.
#
#   tests/record.sh <case>
#
# <case> names tests/scripts/<case>.script; the log goes to tests/expected/<case>.calls. The
# recording is the case's ground truth, so the flags below are part of the case and not a matter
# of taste — they are what tests/diff.sh reproduces on the recomp's side:
#
#   --load-on-open   Lost's opens *are* loads: it hands `#3` a buffer and never issues a read
#                    (reference/eapp-loader/lib.rs). Off by default in the emulator.
#   --allow-creates  the game writes files; without this its writes fail and it takes a
#                    different path.
#   --fixed-clock    the µs clock advances a fixed step per call rather than following the wall,
#                    so two runs of the same script see the same time pass.
#   --fps=0          no frame limiter: a recording should not depend on how fast this machine is.
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
    --load-on-open --allow-creates --fixed-clock --fps=0 \
    --script="$script" --call-log="$expected" \
    > "$here/build/record-$case_name.stdout" 2>&1

printf 'recorded %s: %s calls\n' "$case_name" "$(wc -l < "$expected" | tr -d ' ')"
