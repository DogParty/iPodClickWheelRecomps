#!/bin/sh
# Record an oracle case: run a script through the pinned emulator and keep the framework-call log.
#
#   tests/record.sh <case>
#
# <case> names tests/scripts/<case>.script; the log goes to tests/expected/<case>.calls. The
# recording is the case's ground truth, so the flags below are part of the case and not a matter
# of taste — they are what tests/diff.sh reproduces on the recomp's side:
#
#   --load-on-open       an open that carries a buffer is a load: the bytes arrive with the
#                        completion (reference/eapp-loader/lib.rs; PLAN.md difference 3). Off by
#                        default in the emulator.
#   (async files)        ...and the completion is delivered between frames, not from inside the
#                        open. This is the emulator's default for every title (`--sync-files` is
#                        the flag that turns it off), so there is nothing to pass — noted because
#                        the recomp's file layer has to do the same.
#   --frame-reason=auto  the reason byte is half of a handshake: 0 until the game answers in
#                        `ctx+0x100`, 1 after (PLAN.md difference 1). A constant either way
#                        leaves the game rebuilding itself forever or never starting.
#   --ctx-seed=5         what the init vectors see in the reason byte — the emulator's default,
#                        which this title never reads before the handshake begins.
#   --allow-creates      the game creates `savefile.dat`; without this the create fails and it
#                        takes a different path.
#   --fixed-clock        the µs clock advances a fixed step per call rather than following the
#                        wall, so two runs of the same script see the same time pass. Under it
#                        the resource loader is done by frame 327; on the wall clock it takes
#                        tens of thousands of frames.
#   --fps=0              no frame limiter: a recording should not depend on how fast this
#                        machine is.
#
# The emulator's own defaults table gives a binary named `SimsBowling*` the first three of these
# already (`defaults_for`, play.rs); they are spelled out so that the case does not depend on a
# table in another tree. Buttons go by the event list: the emulator selects that itself for a
# title with no button-flags word, and there is no flag for it to spell out.
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
    --load-on-open --frame-reason=auto --ctx-seed=5 \
    --allow-creates --fixed-clock --fps=0 \
    --script="$script" --call-log="$expected" \
    > "$here/build/record-$case_name.stdout" 2>&1

printf 'recorded %s: %s calls\n' "$case_name" "$(wc -l < "$expected" | tr -d ' ')"
