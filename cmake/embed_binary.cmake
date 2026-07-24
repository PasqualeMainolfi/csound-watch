if(NOT DEFINED INPUT_FILE OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "embed_binary.cmake requires INPUT_FILE and OUTPUT_FILE")
endif()

file(READ "${INPUT_FILE}" _watch_binary_hex HEX)
string(LENGTH "${_watch_binary_hex}" _watch_hex_length)
math(EXPR _watch_binary_size "${_watch_hex_length} / 2")

string(CONCAT _watch_header
"#ifndef WATCH_FONT_DATA_H\n"
"#define WATCH_FONT_DATA_H\n\n"
"#include <stddef.h>\n\n"
"static const unsigned char WATCH_FONT_DATA[] = {\n")

set(_watch_offset 0)
while(_watch_offset LESS _watch_hex_length)
    math(EXPR _watch_remaining "${_watch_hex_length} - ${_watch_offset}")
    if(_watch_remaining GREATER 24)
        set(_watch_chunk_length 24)
    else()
        set(_watch_chunk_length "${_watch_remaining}")
    endif()

    string(SUBSTRING
        "${_watch_binary_hex}"
        "${_watch_offset}"
        "${_watch_chunk_length}"
        _watch_chunk)
    string(REGEX REPLACE
        "([0-9a-f][0-9a-f])"
        "0x\\1, "
        _watch_chunk
        "${_watch_chunk}")
    string(APPEND _watch_header "    ${_watch_chunk}\n")
    math(EXPR _watch_offset "${_watch_offset} + ${_watch_chunk_length}")
endwhile()

string(APPEND _watch_header
"};\n\n"
"static const size_t WATCH_FONT_DATA_SIZE = ${_watch_binary_size}U;\n\n"
"#endif\n")

get_filename_component(_watch_output_directory "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${_watch_output_directory}")
file(WRITE "${OUTPUT_FILE}" "${_watch_header}")
