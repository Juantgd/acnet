# Copyright (c) 2026 juantgd. All Rights Reserved.

cmake_minimum_required(VERSION 3.15)

project(acnet VERSION 0.0.1 LANGUAGES C CXX)

configure_file(
  ${CMAKE_SOURCE_DIR}/version.h.in
  ${CMAKE_BINARY_DIR}/version.h
  @ONLY
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Wpedantic -Wconversion)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/lib)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release")
endif()

# include(FetchContent)

# message(STATUS "fetching glaze and compile...")
# FetchContent_Declare(
#   glaze
#   GIT_REPOSITORY https://github.com/stephenberry/glaze.git
#   GIT_TAG d0c23223c821989096411376e3bcb494b5f8b650 # v7.3.2
#   GIT_SHALLOW TRUE
# )
# FetchContent_MakeAvailable(glaze)

# message(STATUS "fetching quill and compile...")
# FetchContent_Declare(
#   quill
#   GIT_REPOSITORY https://github.com/odygrd/quill.git
#   GIT_TAG 7c0ffa54e51c2b8db6ec091c2922aeaf9b3b08c0 # v11.1.0
#   GIT_SHALLOW TRUE
# )
# FetchContent_MakeAvailable(quill)

add_subdirectory("third_party/glaze")
add_subdirectory("third_party/quill")

include_directories(
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_BINARY_DIR}
)

add_subdirectory(src/core)
add_subdirectory(src/modules)

add_custom_target(
  copy_config ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/config
    ${CMAKE_BINARY_DIR}/bin/config
  COMMENT "copying config files..."
)
