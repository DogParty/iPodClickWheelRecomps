# Cross-compiling for 64-bit Windows with MinGW-w64, from the container windows-build.sh (beside
# this file) runs — or from any machine with the `x86_64-w64-mingw32` toolchain on its path.
# Shared by every title: a title's tools/windows-build.sh is a wrapper around the script here.
#
#   cmake -B build-windows -DCMAKE_TOOLCHAIN_FILE=../common/tools/mingw-w64.cmake
#
# The POSIX-threads flavour of the compiler is named deliberately: the software rasteriser draws
# on std::thread (src/libeapp/gles.cpp), and the Win32-threads flavour of older MinGW libstdc++
# builds has no std::thread at all. The C++ runtime is linked in statically so the result is one
# .exe that needs nothing beside it but SDL3.dll.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc-posix)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++-posix)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

# Where the Windows-flavoured libraries live: the toolchain's own sysroot (zlib, from Debian's
# libz-mingw-w64-dev) and SDL3's mingw development package, which the container unpacks to
# /opt/sdl3. MINGW_SDL3_ROOT overrides the latter for a build outside the container.
set(MINGW_SDL3_ROOT "$ENV{MINGW_SDL3_ROOT}")
if(NOT MINGW_SDL3_ROOT)
    set(MINGW_SDL3_ROOT /opt/sdl3)
endif()
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32 ${MINGW_SDL3_ROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# libgcc, libstdc++ and winpthreads inside the executable rather than as three more DLLs, and
# zlib's static library rather than its import library, so SDL3.dll is the only one beside it.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
set(ZLIB_USE_STATIC_LIBS ON)
