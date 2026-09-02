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
    src/ipod/runtime/fatal.cpp
    src/ipod/runtime/runtime.cpp
    src/ipod/libeapp/gles.cpp
    src/ipod/libeapp/heap.cpp
    src/ipod/gamedata/zip.cpp
)
target_include_directories(ipod_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

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
endif()
