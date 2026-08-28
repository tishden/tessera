# Copyright 2026 Denis Tishkov
# SPDX-License-Identifier: Apache-2.0

add_executable(tessera_bench_runtime runtime/bench_runtime.cpp)
target_link_libraries(tessera_bench_runtime PRIVATE tessera::tessera tessera_project_options)
set_target_properties(tessera_bench_runtime PROPERTIES OUTPUT_NAME bench_runtime)

# The compile-time benchmark is driven by benchmarks/run_compile_bench.py, which invokes the compiler
# directly (it has to measure one translation unit at a time, in isolation). This target exists so
# that the benchmark translation unit is built — and kept compiling — by an ordinary `cmake --build`.
add_executable(tessera_bench_compile_tu compile_time/bench_tu.cpp)
target_link_libraries(tessera_bench_compile_tu PRIVATE tessera::tessera)
target_compile_definitions(tessera_bench_compile_tu PRIVATE TESSERA_BENCH_N=32 TESSERA_BENCH_IMPL=3)
