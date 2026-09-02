#!/bin/sh
# The exact oracle on the pure recompilation: every function recompiled, no hand-decompiled
# replacements, every logged register and stack word compared. This is the regression test for
# tools/emit.py and the runtime; the ordinary `tests/diff.sh` (semantic) is the test for the
# decompiled game. Builds into build-recomp/ from a generated tree in build/gen-pure/.
#
#   tests/check-recomp.sh [case ...]      default cases: boot name-entry menus options hole next-hole
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
cd "$here"
python3 tools/emit.py --replaced /dev/null --out build/gen-pure > /dev/null
cmake -B build-recomp -DMINIGOLF_SDL3=OFF -DMINIGOLF_GEN_DIR="$here/build/gen-pure" > /dev/null
cmake --build build-recomp -j8 > /dev/null
status=0
for case_name in ${@:-boot name-entry menus options hole next-hole}; do
    tests/diff.sh "$case_name" build-recomp/minigolf-headless --exact || status=1
done
exit ${status:-0}
