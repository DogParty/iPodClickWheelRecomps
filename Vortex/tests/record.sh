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
#   --ctx-seed=5         what the init vectors see in the reason byte, and what every frame sees
#                        after them: the pump seeds it once and never writes it again (PLAN.md
#                        difference 1). No `--frame-reason`, deliberately — a value there would
#                        make the pump write the byte every frame, which the recordings do not.
#   --allow-creates      the game creates `options` and `stats` in its folder during the boot;
#                        without permission "its loader retries the missing `options` forever and
#                        never leaves the splash" (play.rs, which forces this for the title).
#   --fixed-clock        the µs clock advances a fixed step per call rather than following the
#                        wall, so two runs of the same script see the same time pass. This game
#                        reads the clock several times a frame, so under it the game's own timers
#                        run fast; the recomp reproduces that under --emulator-firmware.
#   --fps=0              no frame limiter: a recording should not depend on how fast this
#                        machine is. (The emulator paces this title at 30 fps by default because
#                        it divides by its frame delta — PLAN.md difference 5 — but under
#                        --fixed-clock the pace never reaches the game's clock, so 0 is safe.)
#   --time=00:00         the wall clock the game draws and *formats* — its main menu shows the
#                        time, and the string it builds is one character shorter before ten
#                        o'clock than after, which the semantic oracle sees as a different
#                        allocation. tests/diff.sh gives the recomp the same time.
#
# The emulator's own defaults table gives a binary named `vortex*` the load-on-open, the seed and
# the asynchronous model already (`defaults_for`, play.rs); they are spelled out so that the case
# does not depend on a table in another tree. Buttons go by the flags word the emulator finds in
# the image (`button flags word at 0x18063e5c`); there is no flag for it to spell out.
#
# The emulator used is the pinned copy under tools/oracle-emulator (reference/MANIFEST.md), never
# the live tree, so a recording stays reproducible while the emulator moves on.
#
# The emulator opens a window, and a click on that window — or a key pressed while it has focus —
# is a button press the script did not make (PLAN.md rule 11). Its output names every press it
# delivers, scripted ones as "script frame N: …" and window ones as "button NAME -> flags bit"
# (its start-up line "button flags word at …" is not one), so a recording
# whose output shows the latter is refused rather than kept.
set -eu

case_name=${1:?usage: tests/record.sh <case>}
here=$(cd "$(dirname "$0")/.." && pwd)
. "$here/tests/game-dir.sh"

script="$here/tests/scripts/$case_name.script"
expected="$here/tests/expected/$case_name.calls"
emulator="$here/build/oracle-emulator/release/play"
stdout="$here/build/record-$case_name.stdout"

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
    --load-on-open --ctx-seed=5 \
    --allow-creates --fixed-clock --fps=0 --time=00:00 \
    --script="$script" --call-log="$expected" \
    > "$stdout" 2>&1

if grep -q '^button .*-> flags bit' "$stdout"; then
    echo "record.sh: $case_name: the emulator's window received a press the script did not make:" >&2
    grep '^button .*-> flags bit' "$stdout" | sort | uniq -c >&2
    echo "record.sh: recording discarded — re-run without touching the window (PLAN.md rule 11)" >&2
    rm -f "$expected"
    exit 1
fi

printf 'recorded %s: %s calls\n' "$case_name" "$(wc -l < "$expected" | tr -d ' ')"
