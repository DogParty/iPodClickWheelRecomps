# Turning a title's SDL build into a macOS .app, and making that .app stand on its own.
#
# A bare Unix executable is not something to hand another person on a Mac: it has no name, no
# icon, no way to be double-clicked out of a Finder window, and it is linked against whatever
# SDL happened to be on the machine that built it — /opt/homebrew, most likely, which the person
# receiving it does not have. `ipod_macos_app_bundle` fixes both halves:
#
#   * The target becomes a bundle, with the Info.plist in this directory describing it (the
#     name Finder shows, the identifier, the version, and that it can draw at Retina
#     resolution — which matters here, because the renderer really does draw at the window's
#     own resolution rather than being stretched to it).
#   * Every non-system library it loads is copied inside, into Contents/Frameworks, and the
#     references to them are rewritten to `@rpath` (tools/macos-bundle-dylibs.sh). What comes
#     out is an .app that runs on a Mac that has never heard of Homebrew.
#
# What it deliberately does not do is sign anything. An unsigned .app downloaded from the
# internet is quarantined by Gatekeeper, and the person opening it has to say so once; the
# release notes say how (common/tools/release.sh writes that into every artifact's readme).
# Signing needs a paid Apple developer account, and is a decision for whoever publishes rather
# than something a build script should assume.

# Where this file is. Taken here rather than inside the function: within a function body
# `CMAKE_CURRENT_LIST_DIR` is whichever list file called it, not this one.
set(IPOD_MAC_BUNDLE_DIR ${CMAKE_CURRENT_LIST_DIR})

# `TARGET` becomes an .app called `DISPLAY_NAME`, identified as `BUNDLE_ID`, carrying its own
# copies of the libraries it loads.
# Any arguments after `VERSION` are libraries to carry into the bundle whatever `otool` says
# about them — a .framework, whose install name already hides where it came from.
function(ipod_macos_app_bundle TARGET DISPLAY_NAME BUNDLE_ID VERSION)
    if(NOT APPLE)
        return()
    endif()
    set(IPOD_BUNDLE_EXECUTABLE ${TARGET})
    set(IPOD_BUNDLE_NAME ${DISPLAY_NAME})
    set(IPOD_BUNDLE_IDENTIFIER ${BUNDLE_ID})
    set(IPOD_BUNDLE_VERSION ${VERSION})
    set(IPOD_BUNDLE_COPYRIGHT "A recompilation of an iPod game. The game's own files are not included.")
    set(PLIST ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}-Info.plist)
    configure_file(${IPOD_MAC_BUNDLE_DIR}/Info.plist.in ${PLIST} @ONLY)

    # The bundle keeps the target's own plain name — `minigolf.app`, holding `minigolf` — so
    # that every path in this tree and its tests stays what it was. The name a person sees is
    # the plist's (`CFBundleName`), and the release script gives the copy it ships the prettier
    # file name to match (common/tools/release.sh).
    set_target_properties(${TARGET} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${PLIST}
        # Where the executable looks for the libraries carried beside it.
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "@executable_path/../Frameworks")

    # After the link, and before anything else looks at it: the libraries in, the paths fixed.
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${IPOD_MAC_BUNDLE_DIR}/../tools/macos-bundle-dylibs.sh
                $<TARGET_BUNDLE_DIR:${TARGET}> $<TARGET_FILE_NAME:${TARGET}> ${ARGN}
        COMMENT "Carrying ${TARGET}'s libraries into its .app"
        VERBATIM)
endfunction()
