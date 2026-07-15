cmake_minimum_required(VERSION 3.22)
project(YAPS1 VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Compiler options ----
if(MSVC)
    add_compile_options(/W4 /WX /utf-8 /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -fsanitize=address,undefined -g)
    add_link_options(-fsanitize=address,undefined)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT APPLE)
    add_compile_definitions(__cpp_concepts=202002L)
endif()

# ---- Dependencies (bundled) ----
include(FetchContent)

# fmt — for formatted logging
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        10.2.1
)
FetchContent_GetProperties(fmt)
if(NOT fmt_POPULATED)
    FetchContent_Populate(fmt)

    # Patch fmt/core.h and fmt/base.h to disable consteval on Apple Clang 21+
    foreach(FMT_HEADER "${fmt_SOURCE_DIR}/include/fmt/core.h" "${fmt_SOURCE_DIR}/include/fmt/base.h")
        if(EXISTS "${FMT_HEADER}")
            file(READ "${FMT_HEADER}" FMT_CONTENT)
            string(REPLACE "#ifndef FMT_CONSTEVAL" "#if defined(__APPLE__)\n#  define FMT_CONSTEVAL\n#endif\n#ifndef FMT_CONSTEVAL" FMT_CONTENT_PATCHED "${FMT_CONTENT}")
            file(WRITE "${FMT_HEADER}" "${FMT_CONTENT_PATCHED}")
        endif()
    endforeach()

    add_subdirectory(${fmt_SOURCE_DIR} ${fmt_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

# SDL3
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_X11 OFF CACHE BOOL "" FORCE)
set(SDL_WAYLAND OFF CACHE BOOL "" FORCE)
set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.2.0
    CMAKE_ARGS     -DSDL_TESTS=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_STATIC=ON -DSDL_SHARED=OFF -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_UNIX_CONSOLE_BUILD=ON
)
FetchContent_MakeAvailable(SDL3)

# ---- Vulkan (optional) ----
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    add_compile_definitions(YAPS1_HAS_VULKAN)
    message(STATUS "Vulkan found — Vulkan renderer will be available")
else()
    message(STATUS "Vulkan NOT found — software renderer only")
endif()

# ---- Project source files ----
file(GLOB_RECURSE YAPS1_SOURCES
    src/*.cpp
    src/*.h
)

add_executable(YAPS1 ${YAPS1_SOURCES})

target_include_directories(YAPS1 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link SDL3 and fmt-header-only
target_link_libraries(YAPS1 PRIVATE SDL3::SDL3 fmt::fmt-header-only)

# Link threading library on non-Windows
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(YAPS1 PRIVATE Threads::Threads)
endif()

# Link Vulkan if available
if(Vulkan_FOUND)
    target_link_libraries(YAPS1 PRIVATE Vulkan::Vulkan)
endif()

# Zlib (for compressed save states)
find_package(ZLIB QUIET)
if(ZLIB_FOUND)
    target_link_libraries(YAPS1 PRIVATE ZLIB::ZLIB)
    target_compile_definitions(YAPS1 PUBLIC YAPS1_HAS_ZLIB)
endif()

# ---- Install ----
install(TARGETS YAPS1 RUNTIME DESTINATION bin)

# ---- Tests ----
enable_testing()

add_executable(test_emulator
    tests/test_emulator.cpp
    src/cpu/cpu.cpp
    src/cpu/cop0.cpp
)

target_include_directories(test_emulator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(test_emulator PRIVATE fmt::fmt-header-only)

add_test(NAME test_emulator COMMAND test_emulator)

# ---- Default config / mapping files ----
# These are copied on first run by the emulator if they don't exist.