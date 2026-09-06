cmake_minimum_required(VERSION 3.20)
project(philidor VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------
# Global project configuration
# ---------------------------------------------------------

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Default build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Use ccache if available for build speed
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER   ${CCACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()

# ---------------------------------------------------------
# FetchContent setup
# ---------------------------------------------------------
include(FetchContent)

# Disable test/benchmark builds for EVE
set(EVE_BUILD_TEST       OFF CACHE BOOL "" FORCE)
set(EVE_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    eve
    GIT_REPOSITORY https://github.com/jfalcou/eve.git
    GIT_TAG        v2023.02.15
    SYSTEM
)
FetchContent_MakeAvailable(eve)

# ---------------------------------------------------------
# Executable target
# ---------------------------------------------------------
add_executable(philidor)

target_sources(philidor PRIVATE
    src/main.cpp
    src/board.cpp
    src/magic.cpp
    src/nnue.cpp
    src/tt.cpp
    src/uci.cpp
    src/zobrist.cpp
    src/search.cpp
)

target_link_libraries(philidor PRIVATE eve::eve)

# Enable LTO specifically for this target
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_ok)
if(ipo_ok)
    set_target_properties(philidor PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# ---------------------------------------------------------
# Compiler flags
# ---------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_link_options(philidor PRIVATE "-fuse-ld=lld")
    endif()

    option(USE_AVX512 "Compile with AVX-512 support" OFF)
    option(USE_NATIVE "Compile for the host architecture (-march=native)" ON)

    # Detect architecture
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
        if(USE_NATIVE)
            target_compile_options(philidor PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-mcpu=native>)
        endif()
        message(STATUS "> Architecture detected: ARM64 / NEON")
    else()
        if(USE_NATIVE)
            target_compile_options(philidor PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-march=native>)
            message(STATUS "> Architecture detection: Native host architecture (-march=native)")
        elseif(USE_AVX512)
            target_compile_options(philidor PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-march=x86-64-v4>)
            message(STATUS "> Architecture detected: AVX-512 / x86-64-v4")
        else()
            target_compile_options(philidor PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-march=x86-64-v3>)
            message(STATUS "> Architecture detected: AVX2 / x86-64-v3")
        endif()
    endif()

    # Base performance flags
    target_compile_options(philidor PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>"
        "$<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>"
    )

    # Warnings
    target_compile_options(philidor PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wnon-virtual-dtor
        -Wno-missing-field-initializers
        -Wno-missing-braces
        -Wold-style-cast
        -Wdouble-promotion
        -Wcast-align
        -Wimplicit-fallthrough
        -Wunreachable-code
        -Wnull-dereference
        -Wundef
        -Wunused-macros
        -Wextra-semi
        -Wzero-as-null-pointer-constant
        -Wsuggest-override
    )

    # Optimization flags for Release
    target_compile_options(philidor PRIVATE
        "$<$<CONFIG:Release>:-O3>"
        "$<$<CONFIG:Release>:-DNDEBUG>"
        "$<$<CONFIG:Release>:-fomit-frame-pointer>"
    )
    
    # Optimization flags for Debug
    target_compile_options(philidor PRIVATE
        "$<$<CONFIG:Debug>:-O0>"
        "$<$<CONFIG:Debug>:-g3>"
    )
endif()

# ---------------------------------------------------------
# Build Options
# ---------------------------------------------------------
option(TUNE_BUILD "Enable parameter tuning via UCI" OFF)
if(TUNE_BUILD)
    target_compile_definitions(philidor PRIVATE TUNE_BUILD)
endif()