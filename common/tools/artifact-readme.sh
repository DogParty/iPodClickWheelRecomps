#!/bin/sh
# The readme that goes inside one artifact: what this is, what the player must supply, and where
# it goes on this platform.
#
#   artifact-readme.sh <display-name> <version> <platform> <game-folder> <data-dir> <exe>
#                      <linux-data-dir>
#
# The last is separate because Linux spells the directory differently from macOS and
# Windows, and not predictably — see common/tools/titles.txt.
#
# Written into every artifact by release.sh. Every one of these builds needs the game's own
# files, which are the player's copy off their own iPod and are in no artifact, so the first
# thing this says is what to fetch and where to put it. The rest is what differs per platform:
# a desktop asks for the folder itself on first run, and the console cannot ask at all.
set -eu

name=${1:?}; version=${2:?}; platform=${3:?}; folder=${4:?}; datadir=${5:?}; exe=${6:?}
linuxdir=${7:-$exe}

cat <<HEADER
$name $version — an iPod game, recompiled
=========================================

This is the game from an iPod, translated to run on this machine. It is the program only.

WHAT YOU HAVE TO SUPPLY
-----------------------

The game's own files: the folder named

    $folder

as it sits on your iPod, under iPod_Control/Games (the games live inside the .bin next to it on
some models; whichever way you get at it, the folder is the thing). Nothing in this download is
the game itself, and nothing here will work without that folder. It is not included because it
is not ours to include: it is the game you bought, and yours to copy off your own device.

HEADER

case "$platform" in
macos)
    cat <<BODY
INSTALLING
----------

1. Drag "$name.app" wherever you keep applications.
2. Open it. The first launch asks for the $folder folder (or a zip of it) with the ordinary
   file browser, checks every file in it against the sizes and checksums the game shipped
   with, and copies it to

       ~/Library/Application Support/$datadir/$folder

   Your saves, settings and key bindings live beside it.

macOS WILL REFUSE TO OPEN IT THE FIRST TIME
-------------------------------------------

This app is not signed with an Apple developer certificate, so a copy downloaded from the
internet is quarantined and macOS says it "cannot be opened because the developer cannot be
verified" or that it "is damaged". It is neither: it is unsigned. To open it anyway, either

  * right-click (or Control-click) the app and choose Open, then Open again in the dialog —
    once per copy, and normal double-clicking works afterwards; or
  * open System Settings ▸ Privacy & Security, where a button offers to open it after the
    first refusal; or
  * remove the quarantine flag yourself in Terminal:

        xattr -dr com.apple.quarantine "$name.app"

BODY
    ;;
windows)
    cat <<BODY
INSTALLING
----------

1. Keep $exe.exe and SDL3.dll together in the same folder. Nothing else is needed.
2. Run $exe.exe. The first launch asks for the $folder folder (or a zip of it) with the
   ordinary file browser, checks every file in it against the sizes and checksums the game
   shipped with, and copies it to

       %APPDATA%\\$datadir\\$folder

   Your saves, settings and key bindings live beside it.

There is no console window; the game is one window. Run it from a terminal and its messages
print there, and anything that stops it starting is said in a message box.

WINDOWS MAY WARN ABOUT IT
-------------------------

The program is not signed with a code-signing certificate, so SmartScreen may say it is from an
unknown publisher. "More info" then "Run anyway" starts it.

BODY
    ;;
linux)
    cat <<BODY
INSTALLING
----------

1. Put "$exe" wherever you like and make sure it is executable (chmod +x $exe).
2. Install SDL3 and zlib from your distribution if you haven't: they are not in here, because a
   Linux program takes its libraries from the machine it runs on. On Fedora that is
   \`sudo dnf install SDL3 zlib-ng-compat\`; on Debian and Ubuntu, \`sudo apt install libsdl3-0 zlib1g\`.
3. Run it. The first launch asks for the $folder folder (or a zip of it) with the ordinary file
   browser, checks every file in it against the sizes and checksums the game shipped with, and
   copies it to

       \$XDG_DATA_HOME/$linuxdir/$folder, or ~/.local/share/$linuxdir/$folder

   from where it is read every time after. Your saves go beside it.

This build was compiled against glibc ${GLIBC_FLOOR:-unknown} and needs that version or newer —
\`ldd --version\` says what you have. If yours is older, build from source; it is a plain
\`cmake -B build && cmake --build build\`, and the source release has the instructions.

There is no settings window on Linux — that one is a Cocoa window on macOS and a Win32 one on
Windows, and nobody has written a third — so the frame rate, the picture scaling and the key
bindings come from the settings file and the defaults. The game's own Options and Cheats screens
work as they do everywhere. There is no background music either; the sound effects are all here.

BODY
    ;;
android)
    cat <<BODY
INSTALLING
----------

An APK for a 64-bit Android device, meant for a handheld with a real gamepad.

1. Allow installing from wherever you are putting it (your file manager will offer), then open
   "$exe.apk" on the device. Or, from a computer with the Android platform-tools:

       adb install $exe.apk

2. Copy the $folder folder onto the device, at exactly

       /sdcard/Android/data/org.ipodrecomp.$exe/files/$folder

   There is no file browser in this build — a folder of a hundred-odd files is not something
   Android's document picker hands over well — so it has to be in that place before you start.
   If it is missing the game says so on screen, with the path, and with what is actually wrong
   if the files are there but damaged.

   Since Android 11 \`adb push\` cannot always write into Android/data. If it refuses:

       adb push $folder /data/local/tmp/
       adb shell "mkdir -p /sdcard/Android/data/org.ipodrecomp.$exe/files"
       adb shell "cp -r /data/local/tmp/$folder /sdcard/Android/data/org.ipodrecomp.$exe/files/"

3. Launch it from the app list. Saves and settings are kept beside the game's files.

The D-pad turns the click wheel, A selects, B is Menu. There is no settings window and no
a name is spelled out on the wheel rather than with the on-screen keyboard. The background
music plays, decoded by the device's own codec. Holding B leaves the game, as holding Menu left it on the iPod.

This APK is signed with a debug key, which is enough to install and play and not enough to
publish. Android may warn you about that; it is the same warning any unsigned or
self-signed app gets. If you ever install a copy signed with a different key you will have to
uninstall this one first.

BODY
    ;;
switch)
    cat <<BODY
INSTALLING
----------

Homebrew, for a console that can run it. Copy two things to the SD card:

    $exe-switch.nro     ->  sdmc:/switch/$exe-switch.nro
    the $folder folder      ->  sdmc:/switch/$exe/$folder/

The second is the game's own files, exactly as they sit on the iPod. The console has no file
browser to ask you for them with, so unlike the desktop builds it cannot fetch them at first
run: they have to be in that exact place before you start. If they are missing the program says
so on screen, with the path it looked in, rather than failing silently.

Load it from the Homebrew Menu. Saves and settings are written beside the game's files on the
SD card.

BODY
    ;;
esac

cat <<'FOOTER'
CONTROLS
--------

The iPod's click wheel and its five buttons, on whatever this machine has. Every one of them can
be rebound; the settings window (Ctrl+, on Windows, ⌘, on macOS) lists them, and the console
build has a controls screen of its own. See the project's README for the defaults.

WHAT ELSE IS IN HERE
--------------------

  COPYING.txt        the GNU General Public License, version 3, which this program is under.
  SDL3-LICENSE.txt   SDL3's licence. The program uses SDL for its window, input and sound, and
                     that licence asks to travel with it.
FOOTER

# Only the Android build carries its C++ standard library inside the program; every other
# platform's comes from the system it runs on, and so is not in the download to be licensed.
if [ "$platform" = android ]; then
    cat <<'EXTRA'
  LIBCXX-LICENSE.txt libc++'s licence. This build links the C++ standard library into the
                     program rather than loading the device's, so a copy of it is in here.
EXTRA
fi

cat <<'FOOTER'

LICENCE
-------

This port is free software under the GNU General Public License, version 3 or later, whose full
text is in COPYING.txt beside this file. You may use it, study it, change it and pass it on, and
anyone you pass a changed version to is owed its source.

The source is published with this release, as the `-source.tar.gz` file released alongside it.
Note that five of the six titles need one more step before they build: the machine-translated
half of those ports is written on your own machine from your own copy of the game, because it is
a translation of that game's binary rather than anything the port authors wrote. The source's
RELEASING.md and each title's README say how.

What the licence covers is the port. The game it runs is not the port authors' to license, and
none of it is in this download.

THE GAME'S FILES ARE STILL NOT IN HERE
--------------------------------------

Worth saying twice. This download is a program that can run the game you own. It contains none
of that game's code, art, music or levels.
FOOTER
