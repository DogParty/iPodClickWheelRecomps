// Where a Windows build's messages go, and how it dies where nobody can read
// them.
//
// The window build is linked as a GUI program (CMakeLists.txt,
// WIN32_EXECUTABLE), so Windows gives it no console of its own: double-clicked,
// the game is a game, with one window and no black rectangle behind it. That
// leaves the program's own messages — the game files it could not find, the
// file browser it opened, the traces — with nowhere to go, and this is the
// answer to that:
//
//   * Started from a terminal, the program joins that terminal
//   (`AttachConsole`) and its
//     messages print there, exactly as they did when it was a console program.
//     Nothing is lost for whoever runs it to see the output.
//   * Started from Explorer, there is no terminal to join and none is made. The
//   messages go to
//     the bit bucket, and anything fatal goes to a message box instead — the
//     one case where the player has to be told something, and the window is
//     about to close.
//
// The headless build keeps its console: it is a command-line tool, and its
// whole output is the point of it. This is only for the window build.
#pragma once

namespace ipod::platform {

// Join the terminal that started this program, if there is one, and make
// `fatal` show what it says (runtime/fatal.h). `program_name` titles the
// message box. Does nothing off Windows, so every platform's `main` can call it
// without a guard of its own.
void windows_console_begin(const char *program_name);

} // namespace ipod::platform
