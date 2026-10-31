zephyr_include_directories(include)

# `git describe --dirty` of the checkout that holds dir (a subdirectory such as
# zmk/app or a zmk-config's config/ reports its repository), or fallback when
# dir is not in a git checkout. Read when CMake configures the build.
function(zmk_device_info_git_version out dir fallback)
    execute_process(
        COMMAND git -C ${dir} describe --dirty --always --tags
        RESULT_VARIABLE result
        OUTPUT_VARIABLE version
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT result EQUAL 0 OR version STREQUAL "")
        set(version "${fallback}")
    endif()
    set(${out} "${version}" PARENT_SCOPE)
endfunction()

if(CONFIG_ZMK_DEVICE_INFO)
    if(CONFIG_ZMK_DEVICE_INFO_STUDIO_RPC)
        zmk_device_info_git_version(ZMK_BUILD_VERSION ${APPLICATION_SOURCE_DIR} "unknown")
        set(ZMK_CONFIG_BUILD_VERSION "")
        if(DEFINED ZMK_CONFIG AND EXISTS "${ZMK_CONFIG}")
            zmk_device_info_git_version(ZMK_CONFIG_BUILD_VERSION ${ZMK_CONFIG} "")
        endif()
        zmk_device_info_git_version(MODULE_BUILD_VERSION ${CMAKE_CURRENT_SOURCE_DIR} "unknown")

        # Inject build timestamp (UTC)
        string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%SZ" UTC)

        target_compile_definitions(app PRIVATE
            ZMK_BUILD_VERSION="${ZMK_BUILD_VERSION}"
            ZMK_CONFIG_BUILD_VERSION="${ZMK_CONFIG_BUILD_VERSION}"
            MODULE_BUILD_VERSION="${MODULE_BUILD_VERSION}"
            BUILD_TIMESTAMP="${BUILD_TIMESTAMP}"
        )

        # Emit a GNU build-id (SHA1 of the linked image) so a running device can
        # be matched back to the exact ELF for crash/error analysis. Zephyr sets
        # `--build-id=none` in its linker `base` property; modules are processed
        # after that on the final link line, so this later flag wins (ld honors
        # the last --build-id). build_id.ld pins the note into flash so it ships
        # in the image and can be read at runtime.
        #
        # Skipped on native_sim/posix: it is a host executable (no flash image to
        # match, and no ELF error analysis) and its two-stage native link discards
        # the .note.gnu.build-id section, which would dangle the boundary symbols.
        if(NOT CONFIG_ARCH_POSIX)
            zephyr_link_libraries(-Wl,--build-id=sha1)
            zephyr_linker_sources(ROM_SECTIONS ${CMAKE_CURRENT_SOURCE_DIR}/src/build_id.ld)
        endif()

        target_sources(app PRIVATE src/studio/device_info_handler.c)

        list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
        include(nanopb)
        set(NANOPB_GENERATE_CPP_APPEND_PATH TRUE)
        set(NANOPB_GENERATE_CPP_STANDALONE OFF)

        # NOTE: adding to app target directly causes build issues, so we create a library instead
        zephyr_library()
        file(GLOB_RECURSE PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/proto/*.proto)

        nanopb_generate_cpp(proto_srcs proto_hdrs RELPATH ${CMAKE_CURRENT_SOURCE_DIR} ${PROTO_FILES})
        target_include_directories(${ZEPHYR_CURRENT_LIBRARY} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
        target_sources(${ZEPHYR_CURRENT_LIBRARY} PRIVATE ${proto_srcs} ${proto_hdrs})

        # app should depend on the generated proto files
        target_include_directories(app PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/proto)
        add_dependencies(app ${ZEPHYR_CURRENT_LIBRARY})
    endif()
endif()
