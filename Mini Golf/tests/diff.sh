#!/bin/sh
# Oracle test: run the headless recomp on a scripted session and diff its framework-call log
# against the emulator's recording of the same script.
#
#   tests/diff.sh <case> [path/to/minigolf-headless] [--lines=N] [--exact]
#
# <case> names tests/scripts/<case>.script and tests/expected/<case>.calls. The expected log was
# recorded with the emulator's `play --call-log` (see PLAN.md "Verification oracle" for the
# flags). Exit status is 0 when the logs are identical; otherwise the first differing call is
# printed with both sides, which is where the recomp's behaviour departs from the emulator's.
#
# --lines=N compares only the first N lines, for checking progress before a case fully passes.
# --exact compares every logged register and stack word (see tests/diff.py for the two modes).
set -eu

case_name=${1:?usage: tests/diff.sh <case> [minigolf-headless] [--lines=N] [--exact]}
shift
binary=build/minigolf-headless
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
script="$here/tests/scripts/$case_name.script"
expected="$here/tests/expected/$case_name.calls"
actual="$here/build/$case_name.actual.calls"

# The game image and its resources. GAME_DIR may be set in the environment; the default is the
# layout the repository was developed against.
: "${GAME_DIR:=$here/../../../20 iPod games/Games_RO/88888}"
image="$GAME_DIR/Executables/Minigolf_1_1_2563296.bin"

# Exit 2 means "cannot run this case", which CTest is told to treat as a skip: the game image
# is the player's own copy and is not in the repository. GAME_DIR in the environment overrides
# where it is looked for.
for required in "$script" "$expected" "$image"; do
    if [ ! -f "$required" ]; then
        echo "diff.sh: missing $required (set GAME_DIR to your copy of the game)" >&2
        exit 2
    fi
done

# The game writes its save files into its own directory, and a save changes what the game
# does at start-up. Each run therefore uses a fresh copy of the game directory (without any
# saves), so the reference copy stays untouched and every run starts the same way.
copy="$here/build/game-$case_name"
if [ ! -f "$copy/Executables/Minigolf_1_1_2563296.bin" ]; then
    rm -rf "$copy"
    cp -R "$GAME_DIR" "$copy"
fi
find "$copy" -name '*.sav' -delete
image="$copy/Executables/Minigolf_1_1_2563296.bin"
GAME_DIR=$copy

# The title screen shows the time of day, so the recordings depend on the hour they were made
# at (a one-digit hour). --time pins the game's clock to one.
clock=07:53

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
status=0
python3 "$here/tests/diff.py" $mode "$expected" "$actual" > "$actual.report" || status=$?
sed "s/^/diff.sh: $case_name: /" "$actual.report"
exit $status
