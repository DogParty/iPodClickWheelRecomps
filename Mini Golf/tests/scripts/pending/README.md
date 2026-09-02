# Scripts waiting on a reference log

**This is mostly over.** A script no longer needs a recording to become a test: `tests/vs-recomp.sh`
runs it through both the hand-decompiled game and the pure recompilation and compares what they ask
the frameworks for, and the pure recompilation is the original code. `pages.script` moved up into
`tests/scripts/` on that basis and is a test now.

What a recording still buys is a witness built by something other than this project. Where one
exists it is used (`tests/diff.sh`), and both oracles run.

Still unwritten, and harder: the pause menu and the score card (`pause_menu.cpp`, 17.0%). The
pause menu opens over a hole but its rows never reach `PHASE_STEADY` in a scripted run, so
`menu_screen_step` dispatches nothing and no selection can be made. Whatever drives it is in the
hole's own state machine, not the menu code; that is the thing to work out first.
