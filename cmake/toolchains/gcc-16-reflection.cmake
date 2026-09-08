# Copyright 2026 Denis Tishkov
# SPDX-License-Identifier: Apache-2.0
#
# GCC 16 is the first *released* compiler that implements P2996, behind -freflection. Point
# TESSERA_GCC16_ROOT at it (or leave it unset and have `g++` on PATH already be GCC 16):
#
#   cmake -S . -B build-reflection -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/gcc-16-reflection.cmake \
#         -DTESSERA_GCC16_ROOT=/opt/gcc-16.2.0 \
#         -DTESSERA_ALGEBRA_BACKEND=REFLECTION
#
# Why a toolchain file rather than target_compile_options: CMake releases that predate GCC 16 do not
# know it can do C++26, so `cxx_std_26` either fails outright or silently settles on an older
# `-std=`. Reflection then switches off and the failure surfaces as a wall of errors from inside
# <meta> that reads like a library bug. Clearing CMAKE_CXX_STANDARD_DEFAULT stops CMake adding any
# -std of its own, and the flags below say what is wanted explicitly. Same trap, same fix, as
# clang-p2996.cmake.

if(DEFINED TESSERA_GCC16_ROOT)
    set(CMAKE_C_COMPILER "${TESSERA_GCC16_ROOT}/bin/gcc")
    set(CMAKE_CXX_COMPILER "${TESSERA_GCC16_ROOT}/bin/g++")
elseif(DEFINED ENV{TESSERA_GCC16_ROOT})
    set(CMAKE_C_COMPILER "$ENV{TESSERA_GCC16_ROOT}/bin/gcc")
    set(CMAKE_CXX_COMPILER "$ENV{TESSERA_GCC16_ROOT}/bin/g++")
endif()

set(CMAKE_CXX_STANDARD_DEFAULT "")

# -fconstexpr-ops-limit is GCC's counterpart of Clang's -fconstexpr-steps: with reflection selected,
# deduplication is an ordinary quadratic loop rather than template instantiation, and the default
# budget runs out on lists of a few hundred elements.
set(CMAKE_CXX_FLAGS_INIT "-std=c++26 -freflection -fconstexpr-ops-limit=1000000000")

# The prebuilt GCC ships its own libstdc++; without the rpath the test binaries pick the system one
# and fail to start.
if(DEFINED TESSERA_GCC16_ROOT)
    set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,-rpath,${TESSERA_GCC16_ROOT}/lib64")
elseif(DEFINED ENV{TESSERA_GCC16_ROOT})
    set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,-rpath,$ENV{TESSERA_GCC16_ROOT}/lib64")
endif()
