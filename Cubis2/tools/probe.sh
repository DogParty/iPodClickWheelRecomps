#!/bin/sh
# An exploratory run of the game through the pinned emulator, kept under analysis/.
#
#   tools/probe.sh <name> [extra play flags...]
#
# <name> names analysis/scripts/<name>.script. The run leaves, under analysis/coverage/:
#
#   <name>-summary.txt      the emulator's own output — frames, quads, ordinals, file ops, the
#                           texture and draw tails at every screenshot
#   edges-<name>.txt        every branch edge the run took (`--callgraph-dump`), which
#                           tools/funcs.py reads as part of the seed
#   <name>-shot-NN.png      each screenshot the script asked for (the `shot` action)
#
# and the framework-call log in build/<name>.calls, which is not evidence in itself (a recording
# that matters is made with tests/record.sh) but is what analysis/ordinals.txt is joined from.
#
# The flags are the recording flags of tests/record.sh, for the same reasons; anything after
# <name> is passed through (`--file-ops=200`, `--watch-pc=…`).
#
# Two things this script does that a bare `play` invocation would get wrong (PLAN.md rule 11):
#
#   * The emulator writes every screenshot to /tmp/ipod-shot-NN.png, a path every title's
#     picture oracle shares, and another run on this machine can overwrite or sweep it within a
#     second. The emulator announces each one on its output as it is written, so the output is
#     read line by line and the file copied the moment its line appears — and the frame number
#     in that line is what names the copy, so a shot from another title's run (a different
#     number, or none) cannot be mistaken for one of ours.
#   * The emulator opens a window, and a click on it is a Select. A run whose output shows such a
#     press is reported as contaminated and its files are left with `.contaminated` appended,
#     so they cannot be read as clean evidence by accident.
set -eu

name=${1:?usage: tools/probe.sh <name> [play flags...]}
shift
here=$(cd "$(dirname "$0")/.." && pwd)
. "$here/tests/game-dir.sh"

script="$here/analysis/scripts/$name.script"
coverage="$here/analysis/coverage"
emulator="$here/build/oracle-emulator/release/play"
summary="$coverage/$name-summary.txt"

[ -f "$script" ] || { echo "probe.sh: no $script" >&2; exit 1; }
[ -x "$emulator" ] || { echo "probe.sh: build the pinned emulator first (reference/MANIFEST.md)" >&2; exit 1; }

game_dir=$(game_dir_for "probe-$name") || exit 2
mkdir -p "$coverage"
rm -f "$coverage/$name-shot-"*.png

# The emulator's stdout goes through this loop, which copies a screenshot on the line that
# announces it: `screenshot -> /tmp/ipod-shot-01.png  (frame 300)`.
"$emulator" "$game_dir/$GAME_IMAGE_PATH" --gamedir="$game_dir" \
    --load-on-open --ctx-seed=5 --allow-creates --fixed-clock --fps=0 --time=00:00 \
    --script="$script" --call-log="$here/build/$name.calls" \
    --callgraph-dump="$coverage/edges-$name.txt" "$@" 2>&1 \
| while IFS= read -r line; do
    printf '%s\n' "$line"
    case $line in
        "screenshot -> "*)
            path=${line#screenshot -> }
            path=${path%%  *}
            frame=${line##*(frame }
            frame=${frame%)}
            cp "$path" "$coverage/$name-shot-$(printf '%05d' "$frame").png" 2>/dev/null \
                || printf 'probe.sh: screenshot at frame %s was gone before it could be copied\n' "$frame"
            ;;
    esac
done > "$summary"

if grep -q '^button .*-> flags bit' "$summary"; then
    echo "probe.sh: $name: the emulator's window received a press the script did not make:" >&2
    grep '^button .*-> flags bit' "$summary" | sort | uniq -c >&2
    for produced in "$summary" "$coverage/edges-$name.txt" "$here/build/$name.calls"; do
        [ -f "$produced" ] && mv "$produced" "$produced.contaminated"
    done
    echo "probe.sh: files kept with .contaminated appended — re-run without touching the window" >&2
    exit 1
fi

printf '%s: %s calls, %s edges, %s screenshot(s) -> analysis/coverage/\n' "$name" \
    "$(wc -l < "$here/build/$name.calls" | tr -d ' ')" \
    "$(wc -l < "$coverage/edges-$name.txt" | tr -d ' ')" \
    "$(ls "$coverage/$name-shot-"*.png 2>/dev/null | wc -l | tr -d ' ')"
