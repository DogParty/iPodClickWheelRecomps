#!/bin/bash
# Compare the hand-decompiled game against the pure recompilation on a script.
#
#   tests/vs-recomp.sh <case>
#
# The other oracle (tests/diff.sh) compares against a log recorded from the emulator, which is why
# new cases could not be added: the emulator no longer reproduces the recordings it made. This
# needs no recording. The pure recompilation in build-recomp/ *is* the original code — every ARM
# instruction translated, nothing hand-written — so running both builds on the same script and
# comparing what they ask the frameworks for tests exactly what the recordings test: whether the
# decompilation still means the same thing.
#
# It is the weaker of the two only in that both sides are built here, from one image; where the
# recordings exist they are the better witness, and both are run.
#
# Exit 2 means "cannot run this case", which CTest treats as a skip: the pure recompilation is
# built by tests/check-recomp.sh, and the game image is the player's own copy.
set -eu

case_name=${1:?usage: tests/vs-recomp.sh <case>}
here=$(cd "$(dirname "$0")/.." && pwd)
cd "$here"

decomp=build/minigolf-headless
recomp=build-recomp/minigolf-headless
script="tests/scripts/$case_name.script"
: "${GAME_DIR:=$here/../../../20 iPod games/Games_RO/88888}"

for required in "$decomp" "$script" "$GAME_DIR/Executables/Minigolf_1_1_2563296.bin"; do
    if [ ! -e "$required" ]; then
        echo "vs-recomp.sh: missing $required (set GAME_DIR to your copy of the game)" >&2
        exit 2
    fi
done
if [ ! -x "$recomp" ]; then
    echo "vs-recomp.sh: no pure recompilation — run tests/check-recomp.sh first" >&2
    exit 2
fi

# Each side gets its own copy of the game's folder: a run writes saves, and a save changes what
# the next run does.
# Anything after the log is passed straight to that build; only the decompiled side takes any.
run() {
    binary=$1
    copy=$2
    log=$3
    shift 3
    if [ ! -f "$copy/Executables/Minigolf_1_1_2563296.bin" ]; then
        rm -rf "$copy"
        cp -R "$GAME_DIR" "$copy"
    fi
    find "$copy" -name '*.sav' -delete
    rm -f "$log"
    "$binary" "$copy/Executables/Minigolf_1_1_2563296.bin" --gamedir="$copy" --script="$script" \
        --call-log="$log" --frames="$frames" --time=07:53 "$@" > "$log.stdout" 2>&1 ||
        echo "vs-recomp.sh: $binary exited with status $? (see $log.stdout)"
}

# The frame to stop at: the script's own `quit` if it has one, and otherwise a little past its
# last action — a script can also end because the game itself has stopped (choosing Exit does).
quit_frame=$(sed -n 's/^\([0-9][0-9]*\): *quit.*/\1/p' "$script" | head -1)
last_frame=$(sed -n 's/^\([0-9][0-9]*\):.*/\1/p' "$script" | sort -n | tail -1)
frames=$(( ${quit_frame:-$(( ${last_frame:-0} + 800 ))} + 1 ))

# Both sides are told to draw only what the original drew: the pure recompilation *is* the
# original code and has no Cheats row or extra statistics in it to compare against, and the flag
# also puts the device's starting volume back to the zero the emulator's stub reported, which is
# what the pure side starts from too (src/game/host_text.h, src/libeapp/include/ipod_eapp.h).
run "$decomp" "build/game-$case_name-decomp" "build/$case_name.decomp.calls" --no-port-additions
run "$recomp" "build/game-$case_name-recomp" "build/$case_name.recomp.calls" --no-port-additions

# The frameworks first, then the pictures: a script with `shot` actions in it also compares the
# framebuffer, which the call log cannot see (it records a buffer's address, never its contents).
# The status is diff.py's, not the pipeline's: piping it through sed would hide a divergence.
status=0
if ! output=$(python3 tests/diff.py "build/$case_name.recomp.calls" "build/$case_name.decomp.calls"); then
    status=1
fi
echo "$output" | sed "s/^/vs-recomp.sh: $case_name: /" 

shots_recomp=$(grep -c '^screenshot' "build/$case_name.recomp.calls.stdout" || true)
if [ "${shots_recomp:-0}" -gt 0 ]; then
    if diff <(grep '^screenshot' "build/$case_name.recomp.calls.stdout" | sed 's/-> [^ ]*//') \
            <(grep '^screenshot' "build/$case_name.decomp.calls.stdout" | sed 's/-> [^ ]*//') \
            > /dev/null; then
        echo "vs-recomp.sh: $case_name: $shots_recomp screenshot(s) identical"
    else
        echo "vs-recomp.sh: $case_name: the pictures differ" >&2
        diff <(grep '^screenshot' "build/$case_name.recomp.calls.stdout") \
             <(grep '^screenshot' "build/$case_name.decomp.calls.stdout") | head -6 >&2
        status=1
    fi
fi
exit ${status:-0}
