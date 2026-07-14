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

# ---- Dependencies (bundled) ----
include(FetchContent)

# fmt — for formatted logging
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        11.0.2
)
FetchContent_MakeAvailable(fmt)

# SDL3
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.2.0
    CMAKE_ARGS     -DSDL_TESTS=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_STATIC=ON -DSDL_SHARED=OFF
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

# Link SDL3 and fmt
target_link_libraries(YAPS1 PRIVATE SDL3::SDL3 fmt::fmt)

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

target_link_libraries(test_emulator PRIVATE fmt::fmt)

add_test(NAME test_emulator COMMAND test_emulator)

# ---- Default config / mapping files ----
# These are copied on first run by the emulator if they don't exist.