// See windows_console.h.
#include "ipod/platform/windows_console.h"

#if defined(_WIN32)

#include "ipod/runtime/fatal.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>

#include <cstdio>
#include <string>

namespace ipod::platform {

namespace {

// What the message box is titled: the program's name, kept for the handler
// below.
std::string program;

// UTF-8, as every string in this program is, to what the wide Win32 calls take.
std::wstring wide(const std::string &text) {
  if (text.empty()) {
    return std::wstring();
  }
  const int needed = MultiByteToWideChar(
      CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
  if (needed <= 0) {
    return std::wstring();
  }
  std::wstring out(static_cast<size_t>(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                      &out[0], needed);
  return out;
}

// A stream onto `name`, or onto nothing at all when there is no console to take
// it: a GUI program's standard handles are invalid, and a `printf` to one is a
// failure on every call.
void redirect(FILE *stream, const char *name, const char *mode) {
  FILE *opened = nullptr;
  if (freopen_s(&opened, name, mode, stream) != 0 || opened == nullptr) {
    (void)freopen_s(&opened, "NUL", mode, stream);
  }
}

// Anything fatal, where there is nobody watching a console: a message box, so
// that the player is told why the window is about to go rather than watching it
// vanish.
void show_fatal(const char *message) {
  MessageBoxW(nullptr, wide(message).c_str(), wide(program).c_str(),
              MB_OK | MB_ICONERROR);
}

} // namespace

void windows_console_begin(const char *program_name) {
  program = program_name != nullptr ? program_name : "";
  // The terminal that started this program, if it was started from one. A GUI
  // program has no console of its own; this borrows its parent's, which is what
  // a player running it from a command line expects, and fails harmlessly when
  // there is nobody to borrow from.
  const bool attached = AttachConsole(ATTACH_PARENT_PROCESS) != 0;
  redirect(stdout, "CONOUT$", "w");
  redirect(stderr, "CONOUT$", "w");
  if (attached) {
    // The messages are UTF-8; the console is not, until it is told.
    SetConsoleOutputCP(CP_UTF8);
    // The shell has already printed its prompt and moved on, so this program's
    // first line would land beside it.
    std::fprintf(stderr, "\n");
  } else {
    set_fatal_handler(show_fatal);
  }
}

} // namespace ipod::platform

#else

namespace ipod::platform {

void windows_console_begin(const char * /*program_name*/) {}

} // namespace ipod::platform

#endif
