#!/bin/sh
# Shared by tests/record.sh, tests/diff.sh, tests/frames.sh and tools/probe.sh: where the game's
# files are, and how a run gets a private copy of them.
#
# The game writes into its own folder, and what it wrote last time changes what it does next
# time, so no run may use the folder the player pointed at — and no run may use another run's.
# `game_dir_for <case>` makes a fresh copy under build/ and prints its path.
#
# GAME_DIR names the player's own copy; the default is the layout this repository was developed
# against. Exit status 2 from these helpers means "cannot run this case", which CTest is told to
# treat as a skip: the game's files are the owner's and are not in the repository.

# The executable's name inside the game's folder. The build id is part of it, so a differently
# built copy of the game needs this changed here and in src/gamedata/manifest.h.
GAME_IMAGE_PATH=Executables/vortex_1_1_2563290.bin

project_root() {
    (cd "$(dirname "$0")/.." && pwd)
}

# Print the reference game directory, or exit 2 with a message if it is not there.
reference_game_dir() {
    here=$(project_root)
    : "${GAME_DIR:=$here/../../../20 iPod games/Games_RO/12345}"
    if [ ! -f "$GAME_DIR/$GAME_IMAGE_PATH" ]; then
        echo "$0: no game at $GAME_DIR (set GAME_DIR to your copy of the game)" >&2
        exit 2
    fi
    printf '%s' "$GAME_DIR"
}

# The files the game writes into its folder, which are not shipped: `options` and `stats` at the
# root, created during the boot, and `<lang>/stats` once a name has been entered (PLAN.md
# difference 3). The reference folder this repository was developed against holds all three from
# an earlier emulator session, and a case that found them would not be a first boot. Every
# private copy starts without them; nothing shipped is called `options` or `stats`, so the names
# are enough.
strip_written_files() {
    rm -f "$1/options" "$1/stats"
    find "$1" -mindepth 2 -maxdepth 2 -name stats -type f -exec rm -f {} +
    # The language folder is the game's too (it holds nothing but its `stats`); an empty one
    # left behind would be a difference from a shipped folder.
    for language_dir in "$1"/??; do
        [ -d "$language_dir" ] && rmdir "$language_dir" 2>/dev/null
    done
    return 0
}

# Print a private copy of the game directory for <case>, making it fresh each time. `cp -c`
# clones on APFS, which makes a 30 MB copy nearly free; other systems fall back to a real copy.
game_dir_for() {
    here=$(project_root)
    source_dir=$(reference_game_dir) || exit 2
    copy="$here/build/game-$1"
    rm -rf "$copy"
    mkdir -p "$here/build"
    cp -Rc "$source_dir" "$copy" 2>/dev/null || cp -R "$source_dir" "$copy"
    strip_written_files "$copy"
    printf '%s' "$copy"
}
