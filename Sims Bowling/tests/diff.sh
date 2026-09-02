#!/bin/sh
# Oracle test: run the headless recomp on a scripted session and diff its framework-call log
# against the emulator's recording of the same script.
#
#   tests/diff.sh <case> [path/to/bowling-headless] [--lines=N] [--exact]
#
# <case> names tests/scripts/<case>.script and tests/expected/<case>.calls. The expected log was
# recorded with the emulator's `play --call-log` (see PLAN.md "Verification oracle" for the
# flags). Exit status is 0 when the logs are identical; otherwise the first differing call is
# printed with both sides, which is where the recomp's behaviour departs from the emulator's.
#
# --lines=N compares only the first N lines, for checking progress before a case fully passes.
# --exact compares every logged register and stack word (see tests/diff.py for the two modes).
set -eu

case_name=${1:?usage: tests/diff.sh <case> [bowling-headless] [--lines=N] [--exact]}
shift
binary=build/bowling-headless
limit=0
mode=""
for argument in "$@"; do
    case $argument in
        --lines=*) limit=${argument#--lines=} ;;
        --exact) mode="--exact" ;;
        --*) echo "diff.sh: unknown option $argument" >&2; exit 2 ;;
        *) binary=$argument ;;
    esac
done

here=$(cd "$(dirname "$0")/.." && pwd)
. "$here/tests/game-dir.sh"
script="$here/tests/scripts/$case_name.script"
expected="$here/tests/expected/$case_name.calls"
actual="$here/build/$case_name.actual.calls"

# Exit 2 means "cannot run this case", which CTest is told to treat as a skip: a recording or
# the player's own copy of the game may not be there.
for required in "$script" "$expected"; do
    if [ ! -f "$required" ]; then
        echo "diff.sh: missing $required" >&2
        exit 2
    fi
done

# A private, freshly copied game directory: the game writes into its folder, and what it wrote
# last time changes what it does this time (tests/game-dir.sh).
GAME_DIR=$(game_dir_for "$case_name") || exit 2
image="$GAME_DIR/$GAME_IMAGE_PATH"

# The clock the game is told it is. Pinned so a recording does not depend on the hour it was made
# at; the recordings are made with the emulator's --fixed-clock, which starts from zero.
clock=00:00

# The emulator runs the frame on which the script says `quit`, so the frame count is one more
# than the quit frame; --frames gives the recomp the same bound as a safety net.
quit_frame=$(sed -n 's/^\([0-9][0-9]*\): *quit.*/\1/p' "$script" | head -1)
frames=$(( ${quit_frame:-0} + 1 ))

rm -f "$actual"  # never compare a log left over from an earlier run
# --emulator-firmware: the recordings were made with the emulator, which sets the game's button-flags
# word directly and never tells it when a button went down. The game reads a press made minutes
# into a session as a button held down for minutes and asks to be suspended, which is a fault of
# the harness rather than of the game (see runtime/main.cpp). The recomp does tell it, so it does
# not suspend — and to compare against those recordings it has to make the same mistake they did.
"$binary" "$image" --gamedir="$GAME_DIR" --script="$script" --call-log="$actual" --frames="$frames" --time=$clock --emulator-firmware \
    > "$here/build/$case_name.stdout" 2>&1 || echo "diff.sh: recomp exited with status $? (see build/$case_name.stdout)"

if [ "$limit" -gt 0 ]; then
    head -n "$limit" "$expected" > "$actual.expected-head"
    head -n "$limit" "$actual" > "$actual.head"
    expected="$actual.expected-head"
    actual="$actual.head"
fi

# Semantic comparison by default (real arguments per ordinal); --exact compares every logged
# word, which only the pure recompilation (no hand-decompiled functions) is expected to pass.
#
# The exact comparison drops the host-time calls (`miscTBD #12`) from both logs. The emulator
# answers them with the machine's real wall clock — `--fixed-clock` fixes the µs timer, not the
# date — and this game asks every frame, into a stack slot the next frame's call then logs, so
# every recording carries the seconds of the minute it was made in. Nothing else is excused:
# the recomp's `--time=` fixes the digits the game draws, and the semantic comparison sees the
# call itself.
status=0
if [ -n "$mode" ] && [ -f "$here/tests/exact-allow.txt" ]; then
    python3 "$here/tests/diff.py" $mode --allow "$here/tests/exact-allow.txt" "$expected" "$actual" \
        > "$actual.report" || status=$?
else
    python3 "$here/tests/diff.py" $mode "$expected" "$actual" > "$actual.report" || status=$?
fi
sed "s/^/diff.sh: $case_name: /" "$actual.report"
exit $status
