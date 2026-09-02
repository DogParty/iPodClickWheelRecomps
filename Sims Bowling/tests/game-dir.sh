#!/bin/sh
# Shared by tests/record.sh and tests/diff.sh: where the game's files are, and how a run gets a
# private copy of them.
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
GAME_IMAGE_PATH=Executables/SimsBowling_1_1_3002478.bin

# The game's save, at the folder's root. It is not a shipped file — the game creates it empty
# during its boot and writes it later — but the reference folder this repository was developed
# against holds one from an earlier emulator session (PLAN.md difference 6), and a case that
# found it would not be a first boot. Every private copy starts without one.
GAME_SAVE_FILE=savefile.dat

project_root() {
    (cd "$(dirname "$0")/.." && pwd)
}

# Print the reference game directory, or exit 2 with a message if it is not there.
reference_game_dir() {
    here=$(project_root)
    : "${GAME_DIR:=$here/../../../20 iPod games/Games_RO/1500C}"
    if [ ! -f "$GAME_DIR/$GAME_IMAGE_PATH" ]; then
        echo "$0: no game at $GAME_DIR (set GAME_DIR to your copy of the game)" >&2
        exit 2
    fi
    printf '%s' "$GAME_DIR"
}

# Print a private copy of the game directory for <case>, making it fresh each time. `cp -c`
# clones on APFS, which makes a 53 MB copy nearly free; other systems fall back to a real copy.
game_dir_for() {
    here=$(project_root)
    source_dir=$(reference_game_dir) || exit 2
    copy="$here/build/game-$1"
    rm -rf "$copy"
    mkdir -p "$here/build"
    cp -Rc "$source_dir" "$copy" 2>/dev/null || cp -R "$source_dir" "$copy"
    rm -f "$copy/$GAME_SAVE_FILE"
    printf '%s' "$copy"
}
