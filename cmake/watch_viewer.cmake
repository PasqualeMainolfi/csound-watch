if(NOT BUILD_WATCH_VIEWER)
    return()
endif()

if(WATCH_USE_SYSTEM_SDL3)
    find_package(SDL3 3.4 REQUIRED CONFIG)
    find_package(SDL3_ttf 3.2 REQUIRED CONFIG)
else()
    set(WATCH_SDL3_SOURCE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL"
        CACHE PATH "Path to the vendored SDL3 source tree")
    set(WATCH_SDL3_TTF_SOURCE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL_ttf"
        CACHE PATH "Path to the vendored SDL_ttf source tree")

    if(NOT EXISTS "${WATCH_SDL3_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "Vendored SDL3 not found at ${WATCH_SDL3_SOURCE_DIR}. "
            "Populate third_party/SDL or configure with "
            "-DWATCH_USE_SYSTEM_SDL3=ON.")
    endif()
    if(NOT EXISTS "${WATCH_SDL3_TTF_SOURCE_DIR}/CMakeLists.txt"
       OR NOT EXISTS "${WATCH_SDL3_TTF_SOURCE_DIR}/external/freetype/CMakeLists.txt")
        message(FATAL_ERROR
            "Vendored SDL_ttf or FreeType not found at "
            "${WATCH_SDL3_TTF_SOURCE_DIR}. Populate third_party/SDL_ttf or "
            "configure with -DWATCH_USE_SYSTEM_SDL3=ON.")
    endif()

    # The viewer is distributed as a self-contained executable. SDL's public
    # CMake target propagates all platform-specific static link dependencies.
    set(SDL_SHARED OFF CACHE BOOL "Build SDL3 as a shared library" FORCE)
    set(SDL_STATIC ON CACHE BOOL "Build SDL3 as a static library" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "Build SDL3_test" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "Build SDL3 tests" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "Build SDL3 examples" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "Install SDL3 separately" FORCE)

    add_subdirectory("${WATCH_SDL3_SOURCE_DIR}" third_party/SDL EXCLUDE_FROM_ALL)

    # SDL_ttf and FreeType are linked statically into the viewer. The labels
    # only need Latin text and numbers, so shaping and color emoji backends
    # would add size without providing useful functionality here.
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build third-party libraries statically" FORCE)
    set(SDLTTF_VENDORED ON CACHE BOOL "Use vendored SDL_ttf dependencies" FORCE)
    set(SDLTTF_HARFBUZZ OFF CACHE BOOL "Disable HarfBuzz text shaping" FORCE)
    set(SDLTTF_PLUTOSVG OFF CACHE BOOL "Disable color emoji support" FORCE)
    set(SDLTTF_INSTALL OFF CACHE BOOL "Do not install SDL_ttf separately" FORCE)
    set(SDLTTF_SAMPLES OFF CACHE BOOL "Do not build SDL_ttf samples" FORCE)
    set(SDLTTF_STRICT ON CACHE BOOL "Require the vendored FreeType dependency" FORCE)
    add_subdirectory(
        "${WATCH_SDL3_TTF_SOURCE_DIR}"
        third_party/SDL_ttf
        EXCLUDE_FROM_ALL)
endif()

set(WATCH_FONT_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/font/AtkinsonHyperlegible-Regular.otf")
set(WATCH_FONT_DATA_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/generated/watch_font_data.h")
add_custom_command(
    OUTPUT "${WATCH_FONT_DATA_HEADER}"
    COMMAND "${CMAKE_COMMAND}"
        "-DINPUT_FILE=${WATCH_FONT_SOURCE}"
        "-DOUTPUT_FILE=${WATCH_FONT_DATA_HEADER}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_binary.cmake"
    DEPENDS
        "${WATCH_FONT_SOURCE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_binary.cmake"
    COMMENT "Embedding the viewer font"
    VERBATIM)
set_source_files_properties("${WATCH_FONT_DATA_HEADER}" PROPERTIES GENERATED TRUE)

if(WIN32)
    add_executable(watch_viewer WIN32
        src/watch_viewer.c
        src/watch_socket.c
        "${WATCH_FONT_DATA_HEADER}")
else()
    add_executable(watch_viewer
        src/watch_viewer.c
        src/watch_socket.c
        "${WATCH_FONT_DATA_HEADER}")
endif()

target_include_directories(watch_viewer PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(watch_viewer PRIVATE
    SDL3::SDL3
    SDL3_ttf::SDL3_ttf)
if(WIN32)
    target_link_libraries(watch_viewer PRIVATE ws2_32)
endif()
set_property(TARGET watch_viewer PROPERTY C_STANDARD 11)
set_property(TARGET watch_viewer PROPERTY C_STANDARD_REQUIRED ON)

install(TARGETS watch_viewer
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .)
