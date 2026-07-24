if(BUILD_WATCH_OPCODES)
    if(DEFINED APIVERSION AND NOT APIVERSION MATCHES "^7\\.0$")
        message(STATUS "watch: skipped (requires Csound API 7, got APIVERSION=${APIVERSION}; configure with -DAPIVERSION=7.0)")
        return()
    endif()

    make_plugin(watch src/watch.c)
    target_sources(watch PRIVATE
        src/watch_process.c
        src/watch_socket.c)
    target_link_libraries(watch PRIVATE ${CMAKE_DL_LIBS})   # dlopen/dlsym/dladdr (empty on win/mac)

    if(WIN32)
        target_link_libraries(watch PRIVATE ws2_32)
    endif()

    # rpath so the bundled sibling libraries (below) resolve from the plugin's own dir
    if(APPLE)
        set_target_properties(watch PROPERTIES BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")
    elseif(UNIX)
        set_target_properties(watch PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
    endif()

endif()
