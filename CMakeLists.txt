# Build definition for the platform-neutral decoder library and its host tests.
# Copyright (c) 2026 Steven Zhu
# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.16)

project(giflib_embedded VERSION 0.1.0 LANGUAGES C)

# Hidden decoder core plus the retained giflib parser and LZW implementation.
set(GIFLIB_DECODER_CORE_SOURCES
    src/gif_decoder_core.c
    vendor/giflib/dgif_lib.c
    vendor/giflib/gifalloc.c
    vendor/giflib/gif_err.c
    vendor/giflib/openbsd-reallocarray.c
)

# Fixed public facade, user-editable port, and hidden implementation.
add_library(giflib_embedded STATIC
    src/gif_decoder.c
    port/gif_porting.c
    ${GIFLIB_DECODER_CORE_SOURCES}
)
add_library(giflib_embedded::giflib_embedded ALIAS giflib_embedded)

# Publish only the facade include directory; giflib internals remain private.
target_include_directories(giflib_embedded
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/port"
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/giflib"
)

set_target_properties(giflib_embedded PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

# Keep warning policy equivalent across the supported compiler families.
if(MSVC)
    target_compile_options(giflib_embedded PRIVATE /W4)
else()
    target_compile_options(giflib_embedded PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

# Install the static library and the sole application-facing header.
install(TARGETS giflib_embedded
    ARCHIVE DESTINATION lib
)
install(FILES include/gif_decoder.h
    DESTINATION include
)

option(GIFLIB_BUILD_TESTS "Build host-side regression tests" ON)
option(GIFLIB_BUILD_EXAMPLES "Build hosted usage examples" OFF)

if(GIFLIB_BUILD_TESTS)
    enable_testing()

    # Apply the common language, warning, and allocation-tracking test setup.
    function(giflib_configure_test target)
        set_target_properties(${target} PROPERTIES
            C_STANDARD 99
            C_STANDARD_REQUIRED YES
            C_EXTENSIONS NO
        )

        if(MSVC)
            target_compile_options(${target} PRIVATE /W4)
        else()
            target_compile_options(${target} PRIVATE
                -Wall
                -Wextra
                -Wpedantic
            )
        endif()

        if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND NOT CMAKE_CROSSCOMPILING)
            target_sources(${target} PRIVATE
                tests/test_allocator.c
            )
            target_compile_definitions(${target} PRIVATE
                GIFLIB_TEST_ALLOC_TRACKING=1
            )
            target_link_options(${target} PRIVATE
                "-Wl,--wrap=malloc"
                "-Wl,--wrap=calloc"
                "-Wl,--wrap=realloc"
                "-Wl,--wrap=free"
            )
        endif()
    endfunction()

    # Exercise retained low-level giflib behavior and portability fixes.
    add_executable(giflib_regression_tests
        tests/test_regression.c
    )
    target_link_libraries(giflib_regression_tests PRIVATE giflib_embedded)
    target_include_directories(giflib_regression_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/giflib"
    )
    giflib_configure_test(giflib_regression_tests)

    # Exercise the public facade and streaming-compositor contract.
    add_executable(gif_decoder_facade_tests
        tests/test_decoder.c
        tests/test_porting.c
        src/gif_decoder.c
        ${GIFLIB_DECODER_CORE_SOURCES}
    )
    target_include_directories(gif_decoder_facade_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/port"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/giflib"
    )
    giflib_configure_test(gif_decoder_facade_tests)

    # Compile each boundary header without relying on accidental prior includes.
    add_library(giflib_private_header_check OBJECT
        tests/private_header_self_contained.c
    )
    target_include_directories(giflib_private_header_check PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/giflib"
    )
    set_target_properties(giflib_private_header_check PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
    )

    add_library(gif_decoder_public_header_check OBJECT
        tests/public_header_self_contained.c
    )
    target_include_directories(gif_decoder_public_header_check PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    set_target_properties(gif_decoder_public_header_check PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
    )

    add_library(gif_decoder_core_header_check OBJECT
        tests/core_header_self_contained.c
    )
    target_include_directories(gif_decoder_core_header_check PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/port"
    )
    set_target_properties(gif_decoder_core_header_check PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
    )

    add_library(gif_porting_header_check OBJECT
        tests/porting_header_self_contained.c
    )
    target_include_directories(gif_porting_header_check PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/port"
    )
    set_target_properties(gif_porting_header_check PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
    )

    # Register only executable host tests; object targets are compile checks.
    add_test(NAME giflib_regression COMMAND giflib_regression_tests)
    add_test(NAME gif_decoder_facade COMMAND gif_decoder_facade_tests)
endif()

if(GIFLIB_BUILD_EXAMPLES)
    # Embedded player reference with a real memory-backed animation resource.
    add_executable(gif_embedded_player_example
        examples/embedded_player/main.c
        examples/embedded_player/demo_animation.c
        examples/embedded_player/gif_porting.c
        examples/embedded_player/hosted_platform.c
    )
    target_link_libraries(gif_embedded_player_example PRIVATE
        giflib_embedded
    )
    target_include_directories(gif_embedded_player_example PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/port"
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/embedded_player"
    )
    set_target_properties(gif_embedded_player_example PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
    )

    if(MSVC)
        target_compile_options(gif_embedded_player_example PRIVATE /W4)
    else()
        target_compile_options(gif_embedded_player_example PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()

    if(GIFLIB_BUILD_TESTS)
        add_test(NAME gif_embedded_player_example
                 COMMAND gif_embedded_player_example)
    endif()
endif()
