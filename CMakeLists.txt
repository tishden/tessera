# Copyright 2026 Denis Tishkov
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)

project(tessera
        VERSION 1.0.0
        DESCRIPTION "Compile-time type-keyed container and type-list algebra for C++23"
        HOMEPAGE_URL "https://github.com/tishden/tessera"
        LANGUAGES CXX)

set(TESSERA_IS_TOP_LEVEL OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(TESSERA_IS_TOP_LEVEL ON)
endif()

# Benchmarks are meaningless in a debug build, so a top-level build defaults to Release.
if(TESSERA_IS_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(TESSERA_BUILD_TESTS      "Build the test suite"      ${TESSERA_IS_TOP_LEVEL})
option(TESSERA_BUILD_EXAMPLES   "Build the examples"        ${TESSERA_IS_TOP_LEVEL})
option(TESSERA_BUILD_BENCHMARKS "Build the benchmarks"      ${TESSERA_IS_TOP_LEVEL})
option(TESSERA_INSTALL          "Generate install rules"    ${TESSERA_IS_TOP_LEVEL})

# AUTO picks the best backend the compiler offers; the other values pin it (used by the
# compile-time benchmark to measure one backend against another on the same toolchain).
set(TESSERA_DEDUP_BACKEND "AUTO" CACHE STRING "Deduplication backend: AUTO | PORTABLE | BUILTIN | REFLECTION")
set_property(CACHE TESSERA_DEDUP_BACKEND PROPERTY STRINGS AUTO PORTABLE BUILTIN REFLECTION)

add_library(tessera INTERFACE)
add_library(tessera::tessera ALIAS tessera)

target_compile_features(tessera INTERFACE cxx_std_23)
target_include_directories(tessera INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

if(NOT TESSERA_DEDUP_BACKEND STREQUAL "AUTO")
    target_compile_definitions(tessera INTERFACE TESSERA_DEDUP_BACKEND=TESSERA_DEDUP_${TESSERA_DEDUP_BACKEND})
    if(TESSERA_DEDUP_BACKEND STREQUAL "REFLECTION")
        target_compile_definitions(tessera INTERFACE TESSERA_ENABLE_REFLECTION_BACKEND=1)
    endif()
endif()

# Warning set applied to the project's own targets only — never pushed onto consumers.
add_library(tessera_project_options INTERFACE)
if(MSVC)
    target_compile_options(tessera_project_options INTERFACE /W4 /permissive-)
else()
    target_compile_options(tessera_project_options INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(TESSERA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(TESSERA_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(TESSERA_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(TESSERA_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(DIRECTORY include/tessera DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(TARGETS tessera EXPORT tesseraTargets)
    install(EXPORT tesseraTargets
            FILE tesseraTargets.cmake
            NAMESPACE tessera::
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tessera)

    write_basic_package_version_file(
            "${CMAKE_CURRENT_BINARY_DIR}/tesseraConfigVersion.cmake"
            VERSION ${PROJECT_VERSION}
            COMPATIBILITY SameMajorVersion
            ARCH_INDEPENDENT)
    configure_package_config_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/tesseraConfig.cmake.in"
            "${CMAKE_CURRENT_BINARY_DIR}/tesseraConfig.cmake"
            INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tessera)
    install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/tesseraConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/tesseraConfigVersion.cmake"
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tessera)
endif()
