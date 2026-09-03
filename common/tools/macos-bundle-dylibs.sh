#!/bin/sh
# Make a macOS .app stand on its own: the libraries it loads, carried inside it.
#
#   macos-bundle-dylibs.sh <path/to/Something.app> <executable-name> [library ...]
#
# Run after the link, from CMake (common/cmake/IpodMacBundle.cmake). A freshly linked executable
# refers to its libraries by wherever they were on the machine that built it — for SDL that is
# `/opt/homebrew/opt/sdl3/lib/libSDL3.0.dylib` or similar, a path nobody else has. Every such
# reference is copied into `Contents/Frameworks` and rewritten to `@rpath`, which the executable
# resolves to that folder (the rpath CMake set). Apple's own libraries, in /usr/lib and
# /System, are left alone: every Mac has those, and copying them is not allowed anyway.
#
# Any further arguments name libraries to carry whether or not `otool` reports them by an
# absolute path. That is for a .framework: SDL's own release build already calls itself
# `@rpath/SDL3.framework/Versions/A/SDL3`, so nothing in the executable says where it came from
# and there would be nothing here to find. CMake passes the path it linked against.
#
# Idempotent: run twice and the second run finds nothing left to do.
set -eu

app=${1:?usage: macos-bundle-dylibs.sh <app> <executable-name> [library ...]}
name=${2:?usage: macos-bundle-dylibs.sh <app> <executable-name> [library ...]}
shift 2
exe="$app/Contents/MacOS/$name"
frameworks="$app/Contents/Frameworks"

[ -f "$exe" ] || { echo "macos-bundle-dylibs.sh: no executable at $exe" >&2; exit 1; }
mkdir -p "$frameworks"

# The frameworks named on the command line. A framework built for release calls itself
# `@rpath/Something.framework/Versions/A/Something`, so the executable carries no clue as to
# where it came from and the `otool` pass below has nothing to go on; CMake passes the path it
# linked against instead. The whole framework is copied, symlinks and all, because that is what
# makes it loadable. Plain dylibs are not handled here: `otool` finds those by itself.
for named in "$@"; do
    case "$named" in
        *.framework/*) ;;
        *) continue ;;
    esac
    root="${named%%.framework/*}.framework"
    [ -d "$root" ] || continue
    base=$(basename "$root")
    if [ ! -d "$frameworks/$base" ]; then
        cp -R "$root" "$frameworks/$base"
        chmod -R u+w "$frameworks/$base"
        echo "  carried $base"
    fi
done

# The libraries this executable loads that are not the system's. Each is a tab-indented line
# ending in its version numbers; taking the text between is what survives a path with a space in
# it, and skips the header line that `otool` prints for *each* architecture of a universal
# binary — a header whose first word is that path. The references already rewritten to @rpath
# are ours from an earlier run.
dependencies() {
    otool -L "$1" | sed -n 's/^	\(.*\) (compatibility version .*$/\1/p'
}

dependencies "$exe" | while read -r lib; do
    case "$lib" in
        /usr/lib/*|/System/*|@rpath/*|@executable_path/*|@loader_path/*) continue ;;
    esac
    [ -f "$lib" ] || { echo "macos-bundle-dylibs.sh: $lib is not there to copy" >&2; exit 1; }
    # The real file, not the version symlink pointing at it, and under the name the executable
    # asks for so that the rewritten reference matches what lands in Frameworks.
    base=$(basename "$lib")
    cp -f "$(readlink -f "$lib" 2>/dev/null || python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$lib")" \
        "$frameworks/$base"
    chmod u+w "$frameworks/$base"
    install_name_tool -id "@rpath/$base" "$frameworks/$base" 2>/dev/null
    install_name_tool -change "$lib" "@rpath/$base" "$exe"
    echo "  carried $base"
done

# A copied library may load others; one more pass over each catches those.
for carried in "$frameworks"/*.dylib; do
    [ -e "$carried" ] || continue
    dependencies "$carried" | while read -r lib; do
        case "$lib" in
            /usr/lib/*|/System/*|@rpath/*|@executable_path/*|@loader_path/*) continue ;;
        esac
        base=$(basename "$lib")
        if [ ! -f "$frameworks/$base" ]; then
            cp -f "$lib" "$frameworks/$base"
            chmod u+w "$frameworks/$base"
            install_name_tool -id "@rpath/$base" "$frameworks/$base" 2>/dev/null
            echo "  carried $base (needed by $(basename "$carried"))"
        fi
        install_name_tool -change "$lib" "@rpath/$base" "$carried"
    done
done

# install_name_tool invalidates the signature of everything it touched, and on Apple silicon an
# invalid signature is not a warning: dyld kills the process at load (SIGKILL, "Code Signature
# Invalid"). Ad-hoc signatures are the floor macOS will run, so everything the rewrites touched
# is re-signed — the carried libraries first, then the bundle, whose seal covers them.
for carried in "$frameworks"/*; do
    [ -e "$carried" ] || continue
    codesign --force --sign - "$carried" 2>/dev/null
done
codesign --force --sign - "$app" 2>/dev/null
