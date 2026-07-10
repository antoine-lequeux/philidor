cmake_minimum_required(VERSION 4.2.1)
project(philidor LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(philidor)

target_sources(philidor PRIVATE
    src/main.cpp
    src/defines.hpp
    src/board.hpp
    src/board.cpp
    src/movelist.hpp
)

include(CheckIPOSupported)
check_ipo_supported(RESULT lto_supported OUTPUT error)
if(lto_supported)
    message(STATUS "Link-Time Optimization (LTO) is supported and enabled.")
    set_property(TARGET philidor PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
else()
    message(WARNING "LTO is not supported: ${error}")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(philidor PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(philidor PRIVATE $<$<CONFIG:Release>:
        -O3
        -DNDEBUG
        -march=native
        -fomit-frame-pointer
    >)
endif()
