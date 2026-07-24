if(NOT BUILD_WATCH_OPCODES OR NOT BUILD_WATCH_VIEWER)
    return()
endif()

if(WIN32)
    set(WATCH_RELEASE_PLATFORM "windows-x86_64")
elseif(APPLE)
    if(CMAKE_OSX_ARCHITECTURES MATCHES "arm64" AND
       CMAKE_OSX_ARCHITECTURES MATCHES "x86_64")
        set(WATCH_RELEASE_PLATFORM "macos-universal")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        set(WATCH_RELEASE_PLATFORM "macos-arm64")
    else()
        set(WATCH_RELEASE_PLATFORM "macos-x86_64")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
       CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    set(WATCH_RELEASE_PLATFORM "linux-x86_64")
else()
    set(WATCH_RELEASE_PLATFORM
        "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
endif()

set(WATCH_RELEASE_NAME "watch-csound7-${WATCH_RELEASE_PLATFORM}.zip")
set(WATCH_RELEASE_DIR "${CMAKE_BINARY_DIR}/dist")
set(WATCH_RELEASE_STAGE "${WATCH_RELEASE_DIR}/watch-release")

add_custom_target(watch_release_package
    COMMAND "${CMAKE_COMMAND}" -E remove_directory "${WATCH_RELEASE_STAGE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${WATCH_RELEASE_STAGE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${WATCH_RELEASE_STAGE}/licenses"
    COMMAND "${CMAKE_COMMAND}" -E copy
            "$<TARGET_FILE:watch>" "${WATCH_RELEASE_STAGE}/"
    COMMAND "${CMAKE_COMMAND}" -E copy
            "$<TARGET_FILE:watch_viewer>" "${WATCH_RELEASE_STAGE}/"
    COMMAND "${CMAKE_COMMAND}" -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL/LICENSE.txt"
            "${WATCH_RELEASE_STAGE}/licenses/SDL3.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL_ttf/LICENSE.txt"
            "${WATCH_RELEASE_STAGE}/licenses/SDL3_ttf.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL_ttf/external/freetype/docs/FTL.TXT"
            "${WATCH_RELEASE_STAGE}/licenses/FreeType.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/font/AtkinsonHyperlegible-OFL.txt"
            "${WATCH_RELEASE_STAGE}/licenses/AtkinsonHyperlegible.txt"
    COMMAND "${CMAKE_COMMAND}" -E chdir "${WATCH_RELEASE_STAGE}"
            "${CMAKE_COMMAND}" -E tar cf
            "${WATCH_RELEASE_DIR}/${WATCH_RELEASE_NAME}"
            --format=zip
            "$<TARGET_FILE_NAME:watch>"
            "$<TARGET_FILE_NAME:watch_viewer>"
            licenses
    DEPENDS watch watch_viewer
    COMMENT "Packaging ${WATCH_RELEASE_NAME} for Risset"
    VERBATIM)
