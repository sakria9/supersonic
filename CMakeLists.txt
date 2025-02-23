﻿# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
cmake_minimum_required (VERSION 3.8)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("supersonic")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# find Jack audio library
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(Jack REQUIRED)

message("Jack include dir: ${Jack_INCLUDE_DIR}")
message("Jack library: ${Jack_LIBRARY}")

# include fmt
add_subdirectory(libs/fmt)
# include boost
add_subdirectory(libs/boost EXCLUDE_FROM_ALL)
# include spdlog
# use external fmt
set(SPDLOG_FMT_EXTERNAL ON)
add_subdirectory(libs/spdlog)
# include cxxopts
add_subdirectory(libs/cxxopts)
# include kissfft
set(KISSFFT_TOOLS OFF)
set(KISSFFT_TEST OFF)
set(KISSFFT_PKGCONFIG OFF)
set(KISSFFT_STATIC ON)
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(libs/kissfft)


# Include sub-projects.
add_subdirectory ("supersonic")
