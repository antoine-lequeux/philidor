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

# Global LTO/IPO for Release
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_ok)
if(ipo_ok)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

# ---------------------------------------------------------
# Global compiler flags (Windows / Clang focused)
# ---------------------------------------------------------

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Disable exceptions and RTTI globally so dependencies build without them
    add_compile_options(
        "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>"
        "$<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>"
    )

    option(USE_AVX512 "Compile with AVX-512 support" OFF)
    option(USE_NATIVE "Compile for the host architecture (-march=native)" ON)

    # Architecture / SIMD baseline
    if(USE_NATIVE)
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-march=native>)
        message(STATUS "> Architecture detection: Native host architecture (-march=native)")
    elseif(USE_AVX512)
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-march=x86-64-v4>)
        message(STATUS "> Architecture detected: AVX-512 / x86-64-v4")
    else()
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-march=x86-64-v3>)
        message(STATUS "> Architecture detected: AVX2 / x86-64-v3")
    endif()
endif()

# ---------------------------------------------------------
# FetchContent setup
# ---------------------------------------------------------
include(FetchContent)

# Disable test/benchmark builds for EVE
set(EVE_BUILD_TEST           OFF CACHE BOOL "" FORCE)
set(EVE_BUILD_BENCHMARKS     OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    eve
    GIT_REPOSITORY https://github.com/jfalcou/eve.git
    GIT_TAG        v2023.02.15
    SYSTEM
)

# Silence all warnings during third-party library compilation.
set(BACKUP_C_FLAGS "${CMAKE_C_FLAGS}")
set(BACKUP_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")
endif()

FetchContent_MakeAvailable(eve)

# Restore original flags for the actual project
set(CMAKE_C_FLAGS "${BACKUP_C_FLAGS}")
set(CMAKE_CXX_FLAGS "${BACKUP_CXX_FLAGS}")

# ---------------------------------------------------------
# Executable target
# ---------------------------------------------------------
add_executable(philidor)

target_sources(philidor PRIVATE
    src/main.cpp
    src/board.cpp
    src/magic.cpp
    src/movegen.cpp
)

target_link_libraries(philidor PRIVATE
    eve::eve
)

# ---------------------------------------------------------
# Optimizations & warnings
# ---------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")

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
        -Wheader-hygiene
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
