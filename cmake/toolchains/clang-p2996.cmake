# Copyright 2026 Denis Tishkov
# SPDX-License-Identifier: Apache-2.0
#
# Toolchain file for the P2996 reference fork of Clang (Bloomberg), which is currently the only way
# to build the reflection backend. Point TESSERA_P2996_ROOT at the unpacked toolchain:
#
#   export TESSERA_P2996_ROOT=$PWD/toolchain
#   cmake -S . -B build-reflection -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/clang-p2996.cmake \
#         -DTESSERA_ALGEBRA_BACKEND=REFLECTION
#
# Why a toolchain file rather than a few -D flags: CMake decides the -std flag itself, and no
# released CMake knows C++26 for this compiler, so it silently pins -std=gnu++2b — which turns
# reflection off and produces errors from inside <meta> that look like a library bug. Clearing
# CMAKE_CXX_STANDARD_DEFAULT stops CMake from adding a -std flag at all, and the standard is passed
# explicitly below.

if(NOT DEFINED ENV{TESSERA_P2996_ROOT})
    message(FATAL_ERROR "Set TESSERA_P2996_ROOT to the root of the unpacked clang-p2996 toolchain")
endif()

set(TESSERA_P2996_ROOT "$ENV{TESSERA_P2996_ROOT}")

set(CMAKE_CXX_COMPILER "${TESSERA_P2996_ROOT}/bin/clang++" CACHE FILEPATH "P2996 fork of Clang")
set(CMAKE_CXX_STANDARD_DEFAULT "")

# -fconstexpr-steps: deduplication is an ordinary quadratic loop under reflection, and the default
# evaluation budget runs out on long lists — the compiler then reports the splice operand as "not a
# constant expression", which is not the obvious symptom of a step limit.
set(CMAKE_CXX_FLAGS_INIT "-std=c++26 -freflection-latest -stdlib=libc++ -fconstexpr-steps=1000000000")

# The fork's libc++ is not on the default search path at run time.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,-rpath,${TESSERA_P2996_ROOT}/lib/x86_64-unknown-linux-gnu")
