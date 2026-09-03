#!/bin/sh
# Build everything a release is made of, and lay it out ready to upload.
#
#   common/tools/release.sh <version> [title ...]
#
# One artifact per title per platform, plus a source tarball and a file of checksums, all under
# `release/<version>/`. With no titles named it does all of them; name some to do only those.
#
# This script is the single description of how a release artifact is made. Continuous
# integration should call it rather than repeat it, so that what a tag builds and what a laptop
# builds cannot drift apart. What it can build depends on where it runs:
#
#   macOS    a .app, on a Mac and nowhere else (it needs Apple's linker and install_name_tool)
#   Windows  a .exe, anywhere Docker runs — the toolchain is in a container
#   Switch   an .nro, anywhere Docker runs — likewise
#
# so a Mac builds all three and a Linux runner builds the last two. Anything it cannot build on
# this machine is skipped with a line saying so, and the run still succeeds; the checksums file
# lists what was actually made.
#
# What is deliberately NOT here: signing. An unsigned .app is quarantined by macOS and an
# unsigned .exe warns under SmartScreen, and each artifact's readme says what to do about that.
# Signing needs paid certificates and secrets, which belong to whoever publishes rather than to
# a build script.
set -eu

version=${1:?usage: release.sh <version> [title ...]}
shift || true

here=$(cd "$(dirname "$0")/../.." && pwd)   # the repository root: every title, and common/
tools="$here/common/tools"
out="$here/release/$version"
titles_file="$tools/titles.txt"

host=$(uname -s)
# SDL's release to build the macOS artifacts against, and the oldest macOS they will run on.
# Both belong to the release rather than to a developer's machine; see sdl3_framework_cmake_dir.
SDL3_RELEASE=3.4.16
MACOS_MINIMUM=11.0

have_docker=no
if command -v docker > /dev/null 2>&1 && docker info > /dev/null 2>&1; then
    have_docker=yes
fi

# The titles asked for, or all of them. Held as a bar-delimited string rather than a list,
# because a title's directory has a space in it ("Mini Golf") and a POSIX shell has no arrays:
# splitting on whitespace would look for a title called "Mini" and another called "Golf".
wanted=""
for arg in "$@"; do
    wanted="$wanted|$arg"
done
[ -n "$wanted" ] && wanted="$wanted|"
selected() {
    [ -z "$wanted" ] && return 0
    case "$wanted" in
        *"|$1|"*) return 0 ;;
    esac
    return 1
}

say() { printf '%s\n' "$*"; }
skip() { printf '  skipped: %s\n' "$*"; }

rm -rf "$out"
mkdir -p "$out"
say "release $version -> $out"
say ""

# ---------------------------------------------------------------------------------------------
# The source tarball: the tree as git has it, which excludes every build directory and every
# byte derived from the original games (see .gitignore). Reproducible from the tag.
# ---------------------------------------------------------------------------------------------
say "source"
if git -C "$here" rev-parse --git-dir > /dev/null 2>&1; then
    prefix="ipod-recomps-$version"
    if git -C "$here" rev-parse "v$version" > /dev/null 2>&1; then
        source_ref="v$version"      # the tag, once there is one
    else
        source_ref=HEAD
        say "  no v$version tag yet — archiving HEAD"
    fi
    git -C "$here" archive --format=tar.gz --prefix="$prefix/" \
        -o "$out/$prefix-source.tar.gz" "$source_ref"
    say "  $prefix-source.tar.gz"
else
    skip "not a git repository, so no source tarball"
fi
say ""

# ---------------------------------------------------------------------------------------------
# One title, one platform.
# ---------------------------------------------------------------------------------------------
# SDL's own macOS build, cached under .release-cache. The one a developer has from Homebrew is
# built for this machine and this macOS: one architecture, and a minimum version equal to
# whatever the machine is running. That is right for developing and wrong for shipping — an app
# built against it runs on Apple silicon, on the newest macOS, and nowhere else. SDL's release
# framework is universal and asks only for macOS 11, so a release is built against that instead.
# Returns the framework's CMake directory, or nothing if it could not be had.
sdl3_framework_cmake_dir() {
    cache="$here/.release-cache"
    framework="$cache/SDL3.framework"
    if [ ! -d "$framework" ]; then
        mkdir -p "$cache"
        dmg="$cache/SDL3-$SDL3_RELEASE.dmg"
        [ -f "$dmg" ] || curl -fsSL -o "$dmg" \
            "https://github.com/libsdl-org/SDL/releases/download/release-$SDL3_RELEASE/SDL3-$SDL3_RELEASE.dmg" \
            || return 1
        mount="$cache/mnt"
        mkdir -p "$mount"
        hdiutil attach -quiet -nobrowse -mountpoint "$mount" "$dmg" || return 1
        cp -R "$mount/SDL3.xcframework/macos-arm64_x86_64/SDL3.framework" "$framework" || true
        hdiutil detach -quiet "$mount" || true
        rmdir "$mount" 2>/dev/null || true
    fi
    [ -d "$framework" ] || return 1
    printf '%s\n' "$framework/Versions/A/Resources/CMake"
}

package_macos() {  # dir exe display folder datadir
    dir=$1; exe=$2; display=$3; folder=$4; datadir=$5
    if [ "$host" != "Darwin" ]; then
        skip "macOS needs a Mac"
        return 0
    fi
    sdl_cmake=$(sdl3_framework_cmake_dir || true)
    if [ -n "$sdl_cmake" ]; then
        sdl_flags="-DSDL3_DIR=$sdl_cmake -DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
    else
        sdl_flags=""
        skip "no SDL3 release framework — building against this machine's SDL, for this machine only"
    fi
    ( cd "$here/$dir" &&
      cmake -B build-release -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOS_MINIMUM" $sdl_flags > /dev/null &&
      cmake --build build-release --target "$exe" -j"$(sysctl -n hw.ncpu)" > /dev/null )
    app="$here/$dir/build-release/$exe.app"
    [ -d "$app" ] || { skip "macOS: no $exe.app was built"; return 0; }
    stage="$out/stage-$exe-macos"
    rm -rf "$stage"; mkdir -p "$stage"
    # The pretty name goes on the copy that ships; the build tree keeps the plain one.
    cp -R "$app" "$stage/$display.app"
    cp "$here/common/licenses/SDL3-LICENSE.txt" "$stage/"
    cp "$here/LICENSE" "$stage/COPYING.txt"
    "$tools/artifact-readme.sh" "$display" "$version" macos "$folder" "$datadir" "$exe" \
        > "$stage/README.txt"
    ( cd "$stage" && zip -q -r -y "$out/$exe-$version-macos.zip" . )
    rm -rf "$stage"
    say "  $exe-$version-macos.zip  ($(arch_of_app "$app"), macOS $MACOS_MINIMUM and later)"
}

# What architectures a built .app actually contains, for the line above.
arch_of_app() {
    lipo -archs "$1/Contents/MacOS/"* 2>/dev/null | head -1 || echo "unknown"
}

package_windows() {  # dir exe display folder datadir
    dir=$1; exe=$2; display=$3; folder=$4; datadir=$5
    if [ "$have_docker" != yes ]; then
        skip "Windows needs Docker (the toolchain is in a container)"
        return 0
    fi
    ( cd "$here/$dir" && ./tools/windows-build.sh > /dev/null )
    built="$here/$dir/build-windows/dist"
    [ -f "$built/$exe.exe" ] || { skip "Windows: no $exe.exe was built"; return 0; }
    stage="$out/stage-$exe-windows"
    rm -rf "$stage"; mkdir -p "$stage"
    cp "$built/$exe.exe" "$built/SDL3.dll" "$stage/"
    cp "$here/common/licenses/SDL3-LICENSE.txt" "$stage/"
    cp "$here/LICENSE" "$stage/COPYING.txt"
    "$tools/artifact-readme.sh" "$display" "$version" windows "$folder" "$datadir" "$exe" \
        > "$stage/README.txt"
    ( cd "$stage" && zip -q -r "$out/$exe-$version-windows-x64.zip" . )
    rm -rf "$stage"
    say "  $exe-$version-windows-x64.zip"
}

package_switch() {  # dir exe display folder datadir
    dir=$1; exe=$2; display=$3; folder=$4; datadir=$5
    if [ "$have_docker" != yes ]; then
        skip "Switch needs Docker (devkitPro is in a container)"
        return 0
    fi
    ( cd "$here/$dir" && ./tools/switch-build.sh > /dev/null 2>&1 ) || true
    nro="$here/$dir/build-switch/$exe-switch.nro"
    [ -f "$nro" ] || { skip "Switch: no $exe-switch.nro was built"; return 0; }
    stage="$out/stage-$exe-switch"
    rm -rf "$stage"; mkdir -p "$stage"
    cp "$nro" "$stage/"
    # No SDL on this one, but the GPL text travels with every binary the licence covers.
    cp "$here/LICENSE" "$stage/COPYING.txt"
    "$tools/artifact-readme.sh" "$display" "$version" switch "$folder" "$datadir" "$exe" \
        > "$stage/README.txt"
    ( cd "$stage" && zip -q -r "$out/$exe-$version-switch.zip" . )
    rm -rf "$stage"
    say "  $exe-$version-switch.zip"
}

# ---------------------------------------------------------------------------------------------
# Every title the table names.
# ---------------------------------------------------------------------------------------------
while IFS='|' read -r dir exe display folder datadir platforms; do
    case "$dir" in ''|\#*) continue ;; esac
    selected "$dir" || continue
    say "$display"
    for platform in $platforms; do
        case "$platform" in
            macos)   package_macos   "$dir" "$exe" "$display" "$folder" "$datadir" ;;
            windows) package_windows "$dir" "$exe" "$display" "$folder" "$datadir" ;;
            switch)  package_switch  "$dir" "$exe" "$display" "$folder" "$datadir" ;;
        esac
    done
    say ""
done < "$titles_file"

# ---------------------------------------------------------------------------------------------
# What was made, and what each file is, so a download can be checked against the release page.
# ---------------------------------------------------------------------------------------------
( cd "$out" && if command -v shasum > /dev/null 2>&1; then
      shasum -a 256 ./*.zip ./*.tar.gz 2>/dev/null | sed 's# \./# #' > SHA256SUMS
  else
      sha256sum ./*.zip ./*.tar.gz 2>/dev/null | sed 's# \./# #' > SHA256SUMS
  fi )
say "checksums"
sed 's/^/  /' "$out/SHA256SUMS"
say ""
say "done: $(find "$out" -maxdepth 1 -name '*.zip' -o -maxdepth 1 -name '*.tar.gz' | wc -l | tr -d ' ') artifacts in $out"
