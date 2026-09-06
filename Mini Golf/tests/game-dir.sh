#!/bin/sh
# Shared by every test that needs the game's own files: where those files are.
#
# The other titles keep this in tests/game-dir.sh too, and for the same reason — the answer was
# repeated in each script, so a copy of the game that did not sit where this repository was
# developed had to be named seven times over.
#
# `resolve_game_dir` sets GAME_DIR when the environment has not. Its callers then make their own
# private copy of it: the game writes saves into its folder, and a save changes what the next run
# does, so no run may use the folder the player pointed at.
#
# Exit status 2 from a caller means "cannot run this case", which CTest is told to treat as a
# skip: the game's files are the owner's and are not in the repository.

# The executable's name inside the game's folder. The build id is part of it, so a differently
# built copy of the game needs this changed here and in src/gamedata/manifest.h.
GAME_IMAGE_PATH=Executables/Minigolf_1_1_2563296.bin
# The folder's name on the iPod, as src/gamedata/manifest.h spells it.
GAME_DIRECTORY_NAME=88888

project_root() {
    (cd "$(dirname "$0")/.." && pwd)
}

# Where the game keeps its files once installed — the same directory src/platform/paths.cpp
# picks, so a checkout that has been played once needs no environment at all. The order matches
# that file: the override first, then the platform's own place for application data.
data_directory() {
    if [ -n "${MINIGOLF_DATA_DIR:-}" ]; then
        printf '%s' "$MINIGOLF_DATA_DIR"
    elif [ -n "${APPDATA:-}" ]; then
        printf '%s' "$APPDATA/iPod Mini Golf"
    elif [ -d "$HOME/Library/Application Support" ]; then
        printf '%s' "$HOME/Library/Application Support/iPod Mini Golf"
    else
        printf '%s' "${XDG_DATA_HOME:-$HOME/.local/share}/ipod-mini-golf"
    fi
}

# Set GAME_DIR, unless the environment already did. Two places are looked at: the layout this
# repository was developed against, and then the installed one — which is where
# `minigolf --install-zip=` puts the files, and so the one a player has.
#
# An explicit GAME_DIR always wins, right or wrong: somebody who set it wants to be told about
# the path they named rather than quietly handed a different one. When neither place has the
# game, GAME_DIR names the installed one, because that is the path worth printing to whoever has
# not installed it yet.
resolve_game_dir() {
    if [ -n "${GAME_DIR:-}" ]; then
        return 0
    fi
    GAME_DIR="$(project_root)/../../../20 iPod games/Games_RO/$GAME_DIRECTORY_NAME"
    if [ ! -f "$GAME_DIR/$GAME_IMAGE_PATH" ]; then
        GAME_DIR="$(data_directory)/$GAME_DIRECTORY_NAME"
    fi
}
