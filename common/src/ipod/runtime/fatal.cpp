// See fatal.h.
#include "ipod/runtime/fatal.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace ipod {

namespace {
void (*fatal_handler)(const char*) = nullptr;
}  // namespace

void set_fatal_handler(void (*handler)(const char* message)) {
    fatal_handler = handler;
}

void fatal(const char* format, ...) {
    char message[512];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(message, sizeof message, format, arguments);
    va_end(arguments);
    std::fflush(stdout);
    std::fprintf(stderr, "fatal: %s\n", message);
    if (fatal_handler != nullptr) {
        fatal_handler(message);
    }
    std::exit(EXIT_FAILURE);
}

}  // namespace ipod
