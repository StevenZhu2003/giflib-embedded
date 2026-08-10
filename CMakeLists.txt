cmake_minimum_required(VERSION 3.16)

project(giflib_embedded VERSION 0.1.0 LANGUAGES C)

add_library(giflib_embedded STATIC
    dgif_lib.c
    gifalloc.c
    gif_err.c
    openbsd-reallocarray.c
)

target_include_directories(giflib_embedded
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

set_target_properties(giflib_embedded PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

if(MSVC)
    target_compile_options(giflib_embedded PRIVATE /W4)
else()
    target_compile_options(giflib_embedded PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

option(GIFLIB_BUILD_TESTS "Build host-side regression tests" ON)

if(GIFLIB_BUILD_TESTS)
    enable_testing()

    add_executable(giflib_regression_tests
        tests/test_regression.c
    )
    target_link_libraries(giflib_regression_tests PRIVATE giflib_embedded)
    set_target_properties(giflib_regression_tests PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
    )

    if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND NOT CMAKE_CROSSCOMPILING)
        target_sources(giflib_regression_tests PRIVATE
            tests/test_allocator.c
        )
        target_compile_definitions(giflib_regression_tests PRIVATE
            GIFLIB_TEST_ALLOC_TRACKING=1
        )
        target_link_options(giflib_regression_tests PRIVATE
            "-Wl,--wrap=malloc"
            "-Wl,--wrap=calloc"
            "-Wl,--wrap=realloc"
            "-Wl,--wrap=free"
        )
    endif()

    add_library(giflib_private_header_check OBJECT
        tests/private_header_self_contained.c
    )
    target_include_directories(giflib_private_header_check PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    set_target_properties(giflib_private_header_check PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
    )

    add_test(NAME giflib_regression COMMAND giflib_regression_tests)
endif()
