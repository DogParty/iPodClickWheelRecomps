#!/bin/sh
# The readme that goes inside one artifact: what this is, what the player must supply, and where
# it goes on this platform.
#
#   artifact-readme.sh <display-name> <version> <platform> <game-folder> <data-dir> <exe>
#
# Written into every artifact by release.sh. Every one of these builds needs the game's own
# files, which are the player's copy off their own iPod and are in no artifact, so the first
# thing this says is what to fetch and where to put it. The rest is what differs per platform:
# a desktop asks for the folder itself on first run, and the console cannot ask at all.
set -eu

name=${1:?}; version=${2:?}; platform=${3:?}; folder=${4:?}; datadir=${5:?}; exe=${6:?}

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

cat <<FOOTER
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
