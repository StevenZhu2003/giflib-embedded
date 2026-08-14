# Build definition for the platform-neutral decoder library and its host tests.
# Copyright (c) 2026 Steven Zhu
# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.16)

project(giflib_embedded VERSION 0.1.0 LANGUAGES C)

option(GIFLIB_ENABLE_HOST_SANITIZERS
       "Enable compiler-supported host sanitizers" OFF)

if(GIFLIB_ENABLE_HOST_SANITIZERS)
    if(CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR
            "GIFLIB_ENABLE_HOST_SANITIZERS is host-only and cannot be used "
            "for a cross-compiled target build")
    endif()

    # MSVC and clang-cl use the MSVC-form ASan option.  MSVC does not offer
    # UBSan, whereas GCC and native Clang provide the ASan+UBSan pair.
    if(MSVC)
        set(GIFLIB_HOST_SANITIZER_COMPILE_OPTIONS
            /fsanitize=address
            /Zi
        )
        if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
            # CMake invokes lld-link directly for a clang-cl target, and
            # lld-link does not accept the compiler-driver spelling. clang-cl
            # records its ASan dependency in the instrumented object files.
            set(GIFLIB_HOST_SANITIZER_LINK_OPTIONS)
        else()
            set(GIFLIB_HOST_SANITIZER_LINK_OPTIONS
                /fsanitize=address
            )
        endif()
        set(GIFLIB_HOST_SANITIZER_DESCRIPTION "AddressSanitizer")

        # clang-cl AddressSanitizer does not support Visual Studio's Debug
        # CRT (/MDd). Select the dynamic release CRT for this opt-in host
        # instrumentation configuration only.
        if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
            set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL" CACHE STRING
                "MSVC runtime required by clang-cl AddressSanitizer" FORCE)
        endif()

        # clang-cl stores compiler-rt below its resource directory, while
        # cl.exe-based ASan installations place the DLL beside the compiler.
        # Ask clang itself when it is the selected C compiler so the runtime
        # lookup also works for the opt-in libFuzzer build.
        if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
            execute_process(
                COMMAND "${CMAKE_C_COMPILER}" --print-runtime-dir
                RESULT_VARIABLE giflib_runtime_dir_result
                OUTPUT_VARIABLE GIFLIB_HOST_SANITIZER_RUNTIME_DIR
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            if(NOT giflib_runtime_dir_result EQUAL 0 OR
               GIFLIB_HOST_SANITIZER_RUNTIME_DIR STREQUAL "")
                message(FATAL_ERROR
                    "GIFLIB_ENABLE_HOST_SANITIZERS could not determine the "
                    "clang-cl compiler runtime directory")
            endif()
            # On Windows, --print-runtime-dir identifies the target-specific
            # library directory (for example lib/x86_64-pc-windows-msvc),
            # whereas the shared ASan runtime resides in the sibling
            # lib/windows directory.
            get_filename_component(GIFLIB_HOST_SANITIZER_RUNTIME_ROOT
                "${GIFLIB_HOST_SANITIZER_RUNTIME_DIR}" DIRECTORY)
            set(GIFLIB_HOST_SANITIZER_RUNTIME_DIR
                "${GIFLIB_HOST_SANITIZER_RUNTIME_ROOT}/windows")
        else()
            get_filename_component(GIFLIB_HOST_SANITIZER_RUNTIME_DIR
                "${CMAKE_C_COMPILER}" DIRECTORY)
        endif()
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(GIFLIB_HOST_SANITIZER_RUNTIME
                "${GIFLIB_HOST_SANITIZER_RUNTIME_DIR}/clang_rt.asan_dynamic-x86_64.dll")
            set(giflib_host_sanitizer_runtime_library_name
                "clang_rt.asan_dynamic-x86_64.lib")
        else()
            set(GIFLIB_HOST_SANITIZER_RUNTIME
                "${GIFLIB_HOST_SANITIZER_RUNTIME_DIR}/clang_rt.asan_dynamic-i386.dll")
            set(giflib_host_sanitizer_runtime_library_name
                "clang_rt.asan_dynamic-i386.lib")
        endif()
        if(NOT EXISTS "${GIFLIB_HOST_SANITIZER_RUNTIME}")
            message(FATAL_ERROR
                "GIFLIB_ENABLE_HOST_SANITIZERS could not find the MSVC "
                "AddressSanitizer runtime beside ${CMAKE_C_COMPILER}")
        endif()
        if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
            find_file(GIFLIB_HOST_SANITIZER_RUNTIME_LIBRARY
                NAMES "${giflib_host_sanitizer_runtime_library_name}"
                PATHS "${GIFLIB_HOST_SANITIZER_RUNTIME_DIR}"
                NO_DEFAULT_PATH)
            if(NOT GIFLIB_HOST_SANITIZER_RUNTIME_LIBRARY)
                message(FATAL_ERROR
                    "GIFLIB_ENABLE_HOST_SANITIZERS could not find the "
                    "clang-cl AddressSanitizer import library")
            endif()
        endif()
    elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        set(GIFLIB_HOST_SANITIZER_COMPILE_OPTIONS
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
        set(GIFLIB_HOST_SANITIZER_LINK_OPTIONS
            -fsanitize=address,undefined
        )
        set(GIFLIB_HOST_SANITIZER_DESCRIPTION
            "AddressSanitizer and UndefinedBehaviorSanitizer")
    else()
        message(FATAL_ERROR
            "GIFLIB_ENABLE_HOST_SANITIZERS requires MSVC, clang-cl, GCC, "
            "or Clang with supported sanitizer runtimes")
    endif()

    if(WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "Clang" AND NOT MSVC)
        execute_process(
            COMMAND "${CMAKE_C_COMPILER}" --print-runtime-dir
            RESULT_VARIABLE giflib_native_clang_runtime_dir_result
            OUTPUT_VARIABLE giflib_native_clang_runtime_dir
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(NOT giflib_native_clang_runtime_dir_result EQUAL 0 OR
           giflib_native_clang_runtime_dir STREQUAL "")
            message(FATAL_ERROR
                "GIFLIB_ENABLE_HOST_SANITIZERS could not determine the "
                "Windows Clang runtime directory")
        endif()
        get_filename_component(giflib_native_clang_runtime_root
            "${giflib_native_clang_runtime_dir}" DIRECTORY)
        set(GIFLIB_HOST_SANITIZER_RUNTIME
            "${giflib_native_clang_runtime_root}/windows/clang_rt.asan_dynamic-x86_64.dll")
        if(NOT EXISTS "${GIFLIB_HOST_SANITIZER_RUNTIME}")
            message(FATAL_ERROR
                "GIFLIB_ENABLE_HOST_SANITIZERS could not find the Windows "
                "Clang runtime DLL")
        endif()
    endif()

    # Accepting a sanitizer flag is insufficient: some host toolchains omit
    # its runtime libraries. Probe a complete link so an unsupported
    # toolchain fails at configuration time, before every test target builds.
    include(CheckCSourceCompiles)
    set(giflib_saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
    set(giflib_saved_required_link_options "${CMAKE_REQUIRED_LINK_OPTIONS}")
    set(giflib_saved_required_libraries "${CMAKE_REQUIRED_LIBRARIES}")
    string(JOIN " " giflib_host_sanitizer_flags
        ${GIFLIB_HOST_SANITIZER_COMPILE_OPTIONS})
    set(CMAKE_REQUIRED_FLAGS
        "${CMAKE_REQUIRED_FLAGS} ${giflib_host_sanitizer_flags}")
    if(MSVC AND CMAKE_C_COMPILER_ID STREQUAL "Clang")
        # CheckCSourceCompiles creates a Debug try-compile target before the
        # project targets inherit CMAKE_MSVC_RUNTIME_LIBRARY. Override its
        # default /MDd explicitly so clang-cl can link the check program.
        set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} /MD")
    endif()
    list(APPEND CMAKE_REQUIRED_LINK_OPTIONS
        ${GIFLIB_HOST_SANITIZER_LINK_OPTIONS})
    if(MSVC AND CMAKE_C_COMPILER_ID STREQUAL "Clang")
        list(APPEND CMAKE_REQUIRED_LIBRARIES
            "${GIFLIB_HOST_SANITIZER_RUNTIME_LIBRARY}")
    endif()
    # The probe follows the selected compiler and runtime flags. Do not retain
    # an earlier negative result after a host compiler/runtime update.
    unset(GIFLIB_HOST_SANITIZER_RUNTIME_AVAILABLE CACHE)
    check_c_source_compiles(
        "int main(void) { return 0; }"
        GIFLIB_HOST_SANITIZER_RUNTIME_AVAILABLE)
    set(CMAKE_REQUIRED_FLAGS "${giflib_saved_required_flags}")
    set(CMAKE_REQUIRED_LINK_OPTIONS "${giflib_saved_required_link_options}")
    set(CMAKE_REQUIRED_LIBRARIES "${giflib_saved_required_libraries}")

    if(NOT GIFLIB_HOST_SANITIZER_RUNTIME_AVAILABLE)
        message(FATAL_ERROR
            "GIFLIB_ENABLE_HOST_SANITIZERS requires a linkable "
            "${GIFLIB_HOST_SANITIZER_DESCRIPTION} runtime from the selected "
            "host compiler")
    endif()
endif()

set(GIF_MEM_BACKEND "BUILTIN" CACHE STRING
    "Memory backend: BUILTIN, PRIVATE, LIBC, or LVGL")
set_property(CACHE GIF_MEM_BACKEND PROPERTY STRINGS BUILTIN PRIVATE LIBC LVGL)
string(TOUPPER "${GIF_MEM_BACKEND}" GIF_MEM_BACKEND_NORMALIZED)

if(NOT GIF_MEM_BACKEND_NORMALIZED STREQUAL "BUILTIN" AND
   NOT GIF_MEM_BACKEND_NORMALIZED STREQUAL "PRIVATE" AND
   NOT GIF_MEM_BACKEND_NORMALIZED STREQUAL "LIBC" AND
   NOT GIF_MEM_BACKEND_NORMALIZED STREQUAL "LVGL")
    message(FATAL_ERROR "GIF_MEM_BACKEND must be BUILTIN, PRIVATE, LIBC, or LVGL")
endif()

option(GIFLIB_ENABLE_DISPOSAL_METHOD_3
       "Enable GIF disposal method 3 (Restore to Previous) support" OFF)
if(GIFLIB_ENABLE_DISPOSAL_METHOD_3)
    set(GIFLIB_DISPOSAL_METHOD_3_VALUE 1)
else()
    set(GIFLIB_DISPOSAL_METHOD_3_VALUE 0)
endif()

set(GIFLIB_LVGL_INCLUDE_DIR "" CACHE PATH
    "Directory containing the public LVGL lvgl.h header (LVGL backend only)")
set(GIFLIB_LVGL_LIBRARY "" CACHE STRING
    "LVGL library used by consumers of giflib_embedded (LVGL backend only)")

# Hidden decoder core plus the retained giflib parser and LZW implementation.
set(GIFLIB_DECODER_CORE_SOURCES
    src/gif_decoder_core.c
    vendor/giflib/dgif_lib.c
    vendor/giflib/gifalloc.c
    vendor/giflib/gif_err.c
)

set(GIFLIB_MEMORY_COMMON_SOURCES
    src/memory/gif_mem.c
)

set(GIFLIB_MEMORY_BUILTIN_SOURCES
    src/memory/gif_mem_builtin.c
    vendor/tlsf/gif_tlsf.c
)

set(GIFLIB_MEMORY_PRIVATE_SOURCES
    port/gif_mem_private.c
)

set(GIFLIB_MEMORY_LIBC_SOURCES
    src/memory/gif_mem_libc.c
)

set(GIFLIB_MEMORY_LVGL_SOURCES
    src/memory/gif_mem_lvgl.c
)

function(giflib_apply_common_options target)
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

    if(GIFLIB_ENABLE_HOST_SANITIZERS)
        target_compile_options(${target} PRIVATE
            ${GIFLIB_HOST_SANITIZER_COMPILE_OPTIONS})
        get_target_property(giflib_target_type ${target} TYPE)
        if(giflib_target_type STREQUAL "EXECUTABLE" OR
           giflib_target_type STREQUAL "SHARED_LIBRARY" OR
           giflib_target_type STREQUAL "MODULE_LIBRARY")
            target_link_options(${target} PRIVATE
                ${GIFLIB_HOST_SANITIZER_LINK_OPTIONS})
            if(MSVC)
                # Keep the MSVC ASan runtime beside local host-test binaries.
                # This command is unreachable in ordinary and target builds.
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${GIFLIB_HOST_SANITIZER_RUNTIME}"
                        "$<TARGET_FILE_DIR:${target}>"
                    VERBATIM)
                if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
                    target_link_libraries(${target} PRIVATE
                        "${GIFLIB_HOST_SANITIZER_RUNTIME_LIBRARY}")
                endif()
            endif()
            if(WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "Clang" AND NOT MSVC)
                # Native Windows Clang links the runtime correctly, but the
                # dynamically loaded DLL must accompany the host executable.
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${GIFLIB_HOST_SANITIZER_RUNTIME}"
                        "$<TARGET_FILE_DIR:${target}>"
                    VERBATIM)
            endif()
        endif()
    endif()
endfunction()

function(giflib_add_memory_backend target backend)
    target_sources(${target} PRIVATE ${GIFLIB_MEMORY_COMMON_SOURCES})

    if(backend STREQUAL "BUILTIN")
        target_sources(${target} PRIVATE ${GIFLIB_MEMORY_BUILTIN_SOURCES})
        target_compile_definitions(${target} PRIVATE
            GIF_MEM_BACKEND=GIF_MEM_USE_BUILTIN
        )
    elseif(backend STREQUAL "PRIVATE")
        target_sources(${target} PRIVATE ${GIFLIB_MEMORY_PRIVATE_SOURCES})
        target_compile_definitions(${target} PRIVATE
            GIF_MEM_BACKEND=GIF_MEM_USE_PRIVATE
        )
    elseif(backend STREQUAL "LIBC")
        target_sources(${target} PRIVATE ${GIFLIB_MEMORY_LIBC_SOURCES})
        target_compile_definitions(${target} PRIVATE
            GIF_MEM_BACKEND=GIF_MEM_USE_LIBC
        )
    elseif(backend STREQUAL "LVGL")
        target_sources(${target} PRIVATE ${GIFLIB_MEMORY_LVGL_SOURCES})
        target_compile_definitions(${target} PRIVATE
            GIF_MEM_BACKEND=GIF_MEM_USE_LVGL
        )
    else()
        message(FATAL_ERROR "Unknown giflib memory backend: ${backend}")
    endif()
endfunction()

# Fixed public facade, user-editable storage port, and hidden implementation.
add_library(giflib_embedded STATIC
    src/gif_decoder.c
    port/gif_porting.c
    ${GIFLIB_DECODER_CORE_SOURCES}
)
add_library(giflib_embedded::giflib_embedded ALIAS giflib_embedded)
giflib_add_memory_backend(giflib_embedded ${GIF_MEM_BACKEND_NORMALIZED})
target_compile_definitions(giflib_embedded PUBLIC
    GIF_ENABLE_DISPOSAL_METHOD_3=${GIFLIB_DISPOSAL_METHOD_3_VALUE}
)

# Publish only the facade include directory; internal boundaries remain private.
target_include_directories(giflib_embedded
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/memory"
        "${CMAKE_CURRENT_SOURCE_DIR}/port"
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/tlsf"
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/giflib"
)
giflib_apply_common_options(giflib_embedded)

if(GIF_MEM_BACKEND_NORMALIZED STREQUAL "LVGL")
    if(GIFLIB_LVGL_INCLUDE_DIR STREQUAL "" OR GIFLIB_LVGL_LIBRARY STREQUAL "")
        message(FATAL_ERROR
            "LVGL backend requires GIFLIB_LVGL_INCLUDE_DIR and GIFLIB_LVGL_LIBRARY")
    endif()
    target_include_directories(giflib_embedded PRIVATE
        "${GIFLIB_LVGL_INCLUDE_DIR}"
    )
    target_link_libraries(giflib_embedded INTERFACE
        "${GIFLIB_LVGL_LIBRARY}"
    )
endif()

# Install the static library, facade header, and centralized configuration.
install(TARGETS giflib_embedded
    ARCHIVE DESTINATION lib
)
install(FILES
    include/gif_decoder.h
    include/gif_config.h
    DESTINATION include
)

option(GIFLIB_BUILD_TESTS "Build host-side regression tests" ON)
option(GIFLIB_BUILD_EXAMPLES "Build hosted usage examples" OFF)
option(GIFLIB_BUILD_LOCAL_COMPATIBILITY_TESTS
       "Build the opt-in host test against a local external GIF corpus" OFF)
set(GIFLIB_LOCAL_COMPATIBILITY_CORPUS_DIR "" CACHE PATH
    "Path to the local gif-conformance corpus (opt-in compatibility test only)")
option(GIFLIB_BUILD_FUZZER
       "Build the host-only clang/libFuzzer decoder harness" OFF)

if(GIFLIB_BUILD_TESTS)
    enable_testing()

    # Exercise retained low-level giflib behavior through the configured library.
    add_executable(giflib_regression_tests
        tests/test_regression.c
    )
    target_link_libraries(giflib_regression_tests PRIVATE giflib_embedded)
    target_include_directories(giflib_regression_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/memory"
        "${CMAKE_CURRENT_SOURCE_DIR}/port"
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/tlsf"
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/giflib"
    )
    giflib_apply_common_options(giflib_regression_tests)

    # Exercise facade failures with a deterministic PRIVATE allocator provider.
    add_executable(gif_decoder_facade_tests
        tests/test_decoder.c
        tests/test_porting.c
        tests/test_allocator.c
        src/gif_decoder.c
        ${GIFLIB_DECODER_CORE_SOURCES}
    )
    giflib_add_memory_backend(gif_decoder_facade_tests PRIVATE)
    target_compile_definitions(gif_decoder_facade_tests PRIVATE
        GIFLIB_TEST_ALLOC_TRACKING=1
        GIF_ENABLE_DISPOSAL_METHOD_3=${GIFLIB_DISPOSAL_METHOD_3_VALUE}
    )
    target_include_directories(gif_decoder_facade_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/memory"
        "${CMAKE_CURRENT_SOURCE_DIR}/port"
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/tlsf"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/giflib"
    )
    giflib_apply_common_options(gif_decoder_facade_tests)

    # Exercise the optional restore-to-previous path independently of the
    # default deliberately-trimmed decoder configuration.
    add_executable(gif_decoder_disposal3_tests
        tests/test_decoder.c
        tests/test_porting.c
        tests/test_allocator.c
        src/gif_decoder.c
        ${GIFLIB_DECODER_CORE_SOURCES}
    )
    giflib_add_memory_backend(gif_decoder_disposal3_tests PRIVATE)
    target_compile_definitions(gif_decoder_disposal3_tests PRIVATE
        GIFLIB_TEST_ALLOC_TRACKING=1
        GIF_ENABLE_DISPOSAL_METHOD_3=1
    )
    target_include_directories(gif_decoder_disposal3_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/memory"
        "${CMAKE_CURRENT_SOURCE_DIR}/port"
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/tlsf"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/giflib"
    )
    giflib_apply_common_options(gif_decoder_disposal3_tests)

    # Verify the backend-independent facade semantics and overflow handling.
    add_executable(gif_memory_semantics_tests
        tests/test_memory.c
        tests/test_allocator.c
    )
    giflib_add_memory_backend(gif_memory_semantics_tests PRIVATE)
    target_compile_definitions(gif_memory_semantics_tests PRIVATE
        GIFLIB_TEST_ALLOC_TRACKING=1
    )
    target_include_directories(gif_memory_semantics_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/memory"
        "${CMAKE_CURRENT_SOURCE_DIR}/port"
    )
    giflib_apply_common_options(gif_memory_semantics_tests)

    # Verify the same facade semantics when its primitive provider is libc.
    add_executable(gif_memory_libc_tests
        tests/test_memory.c
    )
    giflib_add_memory_backend(gif_memory_libc_tests LIBC)
    target_include_directories(gif_memory_libc_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/memory"
    )
    giflib_apply_common_options(gif_memory_libc_tests)

    # Verify the common facade semantics through the actual fixed pool too.
    add_executable(gif_memory_builtin_semantics_tests
        tests/test_memory.c
    )
    giflib_add_memory_backend(gif_memory_builtin_semantics_tests BUILTIN)
    target_include_directories(gif_memory_builtin_semantics_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/memory"
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/tlsf"
    )
    giflib_apply_common_options(gif_memory_builtin_semantics_tests)

    # Exercise the common facade through LVGL 8.4's public allocator names.
    add_executable(gif_memory_lvgl8_tests
        tests/test_memory.c
        tests/lvgl_mock.c
    )
    giflib_add_memory_backend(gif_memory_lvgl8_tests LVGL)
    target_compile_definitions(gif_memory_lvgl8_tests PRIVATE
        GIFLIB_TEST_ALLOC_TRACKING=1
        LVGL_VERSION_MAJOR=8
        LVGL_VERSION_MINOR=4
    )
    target_include_directories(gif_memory_lvgl8_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/memory"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/lvgl_mock"
    )
    giflib_apply_common_options(gif_memory_lvgl8_tests)

    # Exercise the common facade through LVGL 9.x's public allocator names.
    add_executable(gif_memory_lvgl9_tests
        tests/test_memory.c
        tests/lvgl_mock.c
    )
    giflib_add_memory_backend(gif_memory_lvgl9_tests LVGL)
    target_compile_definitions(gif_memory_lvgl9_tests PRIVATE
        GIFLIB_TEST_ALLOC_TRACKING=1
        LVGL_VERSION_MAJOR=9
        LVGL_VERSION_MINOR=0
    )
    target_include_directories(gif_memory_lvgl9_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/memory"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/lvgl_mock"
    )
    giflib_apply_common_options(gif_memory_lvgl9_tests)

    # Stress the actual fixed pool and check TLSF's internal invariants.
    add_executable(gif_memory_builtin_tests
        tests/test_memory_builtin.c
    )
    giflib_add_memory_backend(gif_memory_builtin_tests BUILTIN)
    target_include_directories(gif_memory_builtin_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/memory"
        "${CMAKE_CURRENT_SOURCE_DIR}/port"
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/tlsf"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/giflib"
    )
    giflib_apply_common_options(gif_memory_builtin_tests)

    # Exercise normal public decoder operation through the LIBC backend.
    add_executable(gif_decoder_facade_libc_tests
        tests/test_decoder.c
        tests/test_porting.c
        src/gif_decoder.c
        ${GIFLIB_DECODER_CORE_SOURCES}
    )
    giflib_add_memory_backend(gif_decoder_facade_libc_tests LIBC)
    target_compile_definitions(gif_decoder_facade_libc_tests PRIVATE
        GIF_ENABLE_DISPOSAL_METHOD_3=${GIFLIB_DISPOSAL_METHOD_3_VALUE}
    )
    target_include_directories(gif_decoder_facade_libc_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/memory"
        "${CMAKE_CURRENT_SOURCE_DIR}/port"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/giflib"
    )
    giflib_apply_common_options(gif_decoder_facade_libc_tests)

    # Exercise full decoder ownership and cleanup through the LVGL backend.
    add_executable(gif_decoder_facade_lvgl_tests
        tests/test_decoder.c
        tests/test_porting.c
        tests/lvgl_mock.c
        src/gif_decoder.c
        ${GIFLIB_DECODER_CORE_SOURCES}
    )
    giflib_add_memory_backend(gif_decoder_facade_lvgl_tests LVGL)
    target_compile_definitions(gif_decoder_facade_lvgl_tests PRIVATE
        GIFLIB_TEST_ALLOC_TRACKING=1
        GIF_ENABLE_DISPOSAL_METHOD_3=${GIFLIB_DISPOSAL_METHOD_3_VALUE}
        LVGL_VERSION_MAJOR=9
        LVGL_VERSION_MINOR=0
    )
    target_include_directories(gif_decoder_facade_lvgl_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/memory"
        "${CMAKE_CURRENT_SOURCE_DIR}/port"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/lvgl_mock"
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/giflib"
    )
    giflib_apply_common_options(gif_decoder_facade_lvgl_tests)

    # Compile each boundary header without accidental prior includes.
    add_library(giflib_private_header_check OBJECT
        tests/private_header_self_contained.c
    )
    target_include_directories(giflib_private_header_check PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/memory"
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/tlsf"
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/giflib"
    )
    giflib_apply_common_options(giflib_private_header_check)

    add_library(gif_decoder_public_header_check OBJECT
        tests/public_header_self_contained.c
    )
    target_include_directories(gif_decoder_public_header_check PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    giflib_apply_common_options(gif_decoder_public_header_check)

    add_library(gif_decoder_core_header_check OBJECT
        tests/core_header_self_contained.c
    )
    target_include_directories(gif_decoder_core_header_check PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/port"
    )
    giflib_apply_common_options(gif_decoder_core_header_check)

    add_library(gif_porting_header_check OBJECT
        tests/porting_header_self_contained.c
    )
    target_include_directories(gif_porting_header_check PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/port"
    )
    giflib_apply_common_options(gif_porting_header_check)

    add_library(gif_porting_memory_header_check OBJECT
        tests/porting_memory_header_self_contained.c
    )
    target_include_directories(gif_porting_memory_header_check PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/port"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/memory"
    )
    giflib_apply_common_options(gif_porting_memory_header_check)

    add_library(gif_memory_header_check OBJECT
        tests/memory_header_self_contained.c
    )
    target_include_directories(gif_memory_header_check PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/memory"
        "${CMAKE_CURRENT_SOURCE_DIR}/port"
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/tlsf"
    )
    giflib_apply_common_options(gif_memory_header_check)

    add_test(NAME giflib_regression COMMAND giflib_regression_tests)
    add_test(NAME gif_decoder_facade COMMAND gif_decoder_facade_tests)
    add_test(NAME gif_decoder_disposal3 COMMAND gif_decoder_disposal3_tests)
    add_test(NAME gif_memory_semantics COMMAND gif_memory_semantics_tests)
    add_test(NAME gif_memory_libc COMMAND gif_memory_libc_tests)
    add_test(NAME gif_memory_builtin_semantics
             COMMAND gif_memory_builtin_semantics_tests)
    add_test(NAME gif_memory_lvgl8 COMMAND gif_memory_lvgl8_tests)
    add_test(NAME gif_memory_lvgl9 COMMAND gif_memory_lvgl9_tests)
    add_test(NAME gif_memory_builtin COMMAND gif_memory_builtin_tests)
    add_test(NAME gif_decoder_facade_libc COMMAND gif_decoder_facade_libc_tests)
    add_test(NAME gif_decoder_facade_lvgl COMMAND gif_decoder_facade_lvgl_tests)

    # Deliberately undersize the fixed pool to verify decoder OOM cleanup.
    add_executable(gif_decoder_builtin_oom_tests
        tests/test_decoder_builtin_oom.c
        tests/test_porting.c
        src/gif_decoder.c
        ${GIFLIB_DECODER_CORE_SOURCES}
    )
    giflib_add_memory_backend(gif_decoder_builtin_oom_tests BUILTIN)
    target_compile_definitions(gif_decoder_builtin_oom_tests PRIVATE
        GIF_MEM_POOL_SIZE=4096U
    )
    target_include_directories(gif_decoder_builtin_oom_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/memory"
        "${CMAKE_CURRENT_SOURCE_DIR}/port"
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/tlsf"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/giflib"
    )
    giflib_apply_common_options(gif_decoder_builtin_oom_tests)
    add_test(NAME gif_decoder_builtin_oom COMMAND gif_decoder_builtin_oom_tests)
endif()

# This target is intentionally independent of GIFLIB_BUILD_TESTS: it is an
# opt-in host investigation of Git-ignored external files, not a normal source
# regression or a dependency of consumers. The decoder is built with the same
# PRIVATE test allocator used by facade tests so every case can check cleanup.
if(GIFLIB_BUILD_LOCAL_COMPATIBILITY_TESTS)
    if(GIFLIB_LOCAL_COMPATIBILITY_CORPUS_DIR STREQUAL "" OR
       NOT IS_DIRECTORY "${GIFLIB_LOCAL_COMPATIBILITY_CORPUS_DIR}")
        message(FATAL_ERROR
            "GIFLIB_BUILD_LOCAL_COMPATIBILITY_TESTS requires "
            "GIFLIB_LOCAL_COMPATIBILITY_CORPUS_DIR to name a local "
            "gif-conformance directory")
    endif()

    function(giflib_add_local_compatibility_target target backend smoke)
        set(compatibility_sources
            tests/test_compatibility.c
            tests/test_porting.c
            src/gif_decoder.c
            ${GIFLIB_DECODER_CORE_SOURCES}
        )
        if(backend STREQUAL "PRIVATE")
            list(APPEND compatibility_sources tests/test_allocator.c)
        elseif(backend STREQUAL "LVGL")
            list(APPEND compatibility_sources tests/lvgl_mock.c)
        endif()

        add_executable(${target} ${compatibility_sources})
        giflib_add_memory_backend(${target} ${backend})
        target_compile_definitions(${target} PRIVATE
            GIF_ENABLE_DISPOSAL_METHOD_3=${GIFLIB_DISPOSAL_METHOD_3_VALUE}
        )
        target_include_directories(${target} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/memory"
            "${CMAKE_CURRENT_SOURCE_DIR}/port"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests"
            "${CMAKE_CURRENT_SOURCE_DIR}/vendor/tlsf"
            "${CMAKE_CURRENT_SOURCE_DIR}/vendor/giflib"
        )
        if(backend STREQUAL "PRIVATE")
            target_compile_definitions(${target} PRIVATE
                GIFLIB_TEST_ALLOC_TRACKING=1
            )
        elseif(backend STREQUAL "LVGL")
            target_compile_definitions(${target} PRIVATE
                LVGL_VERSION_MAJOR=9
                LVGL_VERSION_MINOR=0
            )
            target_include_directories(${target} PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/lvgl_mock"
            )
        endif()
        giflib_apply_common_options(${target})

        if(smoke)
            add_test(NAME ${target} COMMAND ${target}
                     "${GIFLIB_LOCAL_COMPATIBILITY_CORPUS_DIR}"
                     --backend-smoke)
        else()
            add_test(NAME ${target} COMMAND ${target}
                     "${GIFLIB_LOCAL_COMPATIBILITY_CORPUS_DIR}")
        endif()
    endfunction()

    enable_testing()
    giflib_add_local_compatibility_target(
        giflib_local_compatibility_tests PRIVATE FALSE)
    giflib_add_local_compatibility_target(
        giflib_local_compatibility_builtin_tests BUILTIN TRUE)
    giflib_add_local_compatibility_target(
        giflib_local_compatibility_libc_tests LIBC TRUE)
    giflib_add_local_compatibility_target(
        giflib_local_compatibility_lvgl_tests LVGL TRUE)
endif()

# The fuzzer is deliberately an explicit host-only investigation target.  It
# uses the public facade and the ordinary in-memory test port, but is not a
# CTest test: libFuzzer owns its own execution loop and corpus state.
if(GIFLIB_BUILD_FUZZER)
    if(CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR
            "GIFLIB_BUILD_FUZZER is host-only and cannot be used for a "
            "cross-compiled target build")
    endif()
    if(NOT GIFLIB_ENABLE_HOST_SANITIZERS)
        message(FATAL_ERROR
            "GIFLIB_BUILD_FUZZER requires GIFLIB_ENABLE_HOST_SANITIZERS=ON")
    endif()
    if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
        message(FATAL_ERROR
            "GIFLIB_BUILD_FUZZER requires a Clang compiler with libFuzzer")
    endif()
    if(MSVC)
        message(FATAL_ERROR
            "GIFLIB_BUILD_FUZZER requires Clang's GNU-like command line; "
            "configure it with clang.exe and Ninja")
    endif()

    add_executable(gif_decoder_fuzzer
        tests/fuzz_decoder.c
        tests/test_porting.c
        src/gif_decoder.c
        ${GIFLIB_DECODER_CORE_SOURCES}
    )
    giflib_add_memory_backend(gif_decoder_fuzzer LIBC)
    target_compile_definitions(gif_decoder_fuzzer PRIVATE
        GIF_ENABLE_DISPOSAL_METHOD_3=${GIFLIB_DISPOSAL_METHOD_3_VALUE}
    )
    target_include_directories(gif_decoder_fuzzer PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/memory"
        "${CMAKE_CURRENT_SOURCE_DIR}/port"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/tlsf"
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/giflib"
    )
    target_compile_options(gif_decoder_fuzzer PRIVATE -fsanitize=fuzzer)
    target_link_options(gif_decoder_fuzzer PRIVATE -fsanitize=fuzzer)
    giflib_apply_common_options(gif_decoder_fuzzer)
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
        "${CMAKE_CURRENT_SOURCE_DIR}/src/memory"
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/embedded_player"
    )
    giflib_apply_common_options(gif_embedded_player_example)

    if(GIFLIB_BUILD_TESTS)
        add_test(NAME gif_embedded_player_example
                 COMMAND gif_embedded_player_example)
    endif()
endif()
