# The shared core, as a library rather than as files to copy. See README.md for why.
#
# `ipod_core` carries no opinion about warnings, sanitizers or optimisation: a title adds this to
# its own strict interface target, so the same sources are held to whichever rules that title
# already applies to its hand-written code. Everything here is namespace `ipod`; a title reaches
# it through the bridging header in its own `src/platform/` (see `shared.h` there).
# Shared code that needs SDL is *not* in this library: a title finds SDL after it adds this
# directory, and the headless oracle target has no SDL at all. It is published as a list instead,
# and each title compiles it into its own SDL executable alongside its own `platform/sdl3/`.
set(IPOD_CORE_SDL3_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ipod/platform/sdl3/music_decoder.cpp
    PARENT_SCOPE)

add_library(ipod_core STATIC
    src/ipod/platform/device.cpp
    src/ipod/platform/save_store.cpp
    src/ipod/platform/text_entry.cpp
    src/ipod/platform/windows_console.cpp
    src/ipod/runtime/fatal.cpp
    src/ipod/runtime/runtime.cpp
    src/ipod/libeapp/gles.cpp
    src/ipod/libeapp/heap.cpp
    src/ipod/gamedata/zip.cpp
)
target_include_directories(ipod_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# One rounding, on every machine.
#
# Clang on ARM — Apple silicon, Android, the Switch — fuses a multiply and an add into a single
# instruction that rounds once where two roundings happened before. It is worth about 4% of the
# frame rate (measured on a Snapdragon 8 Gen 2: 781 frames a second against 752 at the game's own
# size, and 78 against 75 at four times it, where the game asks for 30) and it changes the
# answer: about one pixel in five hundred comes out one value different. That is
# invisible to play and it is enough to stop a frame drawn on one machine from matching a frame
# drawn on another, which is one of the ways this port is checked — the screenshot hashes in each
# title's tests/ compare a build against a recording made somewhere else. Turn the contraction
# off rather than loosen the comparison.
#
# It belongs here, and PUBLIC, for two reasons: the software rasteriser is `libeapp/gles.cpp` in
# this library, which does not inherit the compile options a title sets on its own targets; and
# every title's own code has to round the way the rasteriser does. MSVC is left alone — it has no
# switch that means quite this, and on x64 without /arch:AVX2 it emits no fused multiply-add to
# begin with.
if(NOT MSVC)
    target_compile_options(ipod_core PUBLIC -ffp-contract=off)
endif()

# The rasteriser draws a frame on several threads.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(ipod_core PUBLIC Threads::Threads)
# The zip reader inflates with zlib, and every title's installer computes the CRC-32 of each
# file with it; linking it here means the titles inherit it through `ipod_core`.
find_package(ZLIB REQUIRED)
target_link_libraries(ipod_core PUBLIC ZLIB::ZLIB)
# `platform/device.cpp` asks the host for its battery, and on Apple that is IOKit's power-source
# API — plain C, so no Objective-C, but it does need the two frameworks. Nothing else here links
# a framework; every other platform's answer is a system call or a file.
if(APPLE)
    target_link_libraries(ipod_core PUBLIC "-framework IOKit" "-framework CoreFoundation")
elseif(WIN32)
    # The message box a windowed build dies with (platform/windows_console.cpp).
    target_link_libraries(ipod_core PUBLIC user32)
endif()
