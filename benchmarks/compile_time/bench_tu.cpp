// Copyright 2026 Denis Tishkov
// SPDX-License-Identifier: Apache-2.0
//
// The translation unit measured by run_compile_bench.py. It is compiled repeatedly with a
// different component count and a different implementation, and the compiler's wall time and peak
// resident memory are recorded.
//
//   -DTESSERA_BENCH_N=<count>   how many distinct component types the system ends up with
//   -DTESSERA_BENCH_IMPL=<id>   0 baseline | 1 std::tuple | 2 mosaic | 3 mosaic assembled by tessera
//
// 0 BASELINE  — headers only, so the fixed cost of the toolchain can be subtracted.
// 1 TUPLE     — N components in a std::tuple, addressed by type with std::get<T>.
// 2 MOSAIC    — the same N components in a tessera::mosaic, addressed with get<T>.
//               (1) and (2) together isolate the cost of the container itself.
// 3 ASSEMBLY  — N systems, each declaring four overlapping dependencies: 4N types are
//               flat-mapped and deduplicated down to N. This is the workload that stresses the
//               deduplication backend.

#include <cstddef>
#include <cstdio>
#include <utility>

#define TESSERA_BENCH_BASELINE 0
#define TESSERA_BENCH_TUPLE 1
#define TESSERA_BENCH_MOSAIC 2
#define TESSERA_BENCH_ASSEMBLY 3

#ifndef TESSERA_BENCH_N
#  define TESSERA_BENCH_N 64
#endif
#ifndef TESSERA_BENCH_IMPL
#  define TESSERA_BENCH_IMPL TESSERA_BENCH_BASELINE
#endif

#if TESSERA_BENCH_IMPL == TESSERA_BENCH_TUPLE
#  include <tuple>
#else
#  include <tessera/tessera.hpp>
#endif

namespace {

constexpr std::size_t kComponentCount = TESSERA_BENCH_N;

template<std::size_t I>
struct Component {
    int value = static_cast<int>(I);
};

#if TESSERA_BENCH_IMPL == TESSERA_BENCH_ASSEMBLY

/// Four dependencies per system, overlapping across systems: 4N declarations, N distinct.
template<std::size_t I>
struct System {
    using dependencies =
        tessera::type_list<Component<I>, Component<(I * 7 + 1) % kComponentCount>,
                           Component<(I * 13 + 5) % kComponentCount>, Component<(I + 1) % kComponentCount>>;
};

template<class T>
using dependencies_of = typename T::dependencies;

template<class Sequence>
struct make_systems;

template<std::size_t... Is>
struct make_systems<std::index_sequence<Is...>> {
    using type = tessera::type_list<System<Is>...>;
};

using Systems = typename make_systems<std::make_index_sequence<kComponentCount>>::type;
using System = Systems::flat_map<dependencies_of>::into<tessera::mosaic>;
static_assert(System::size == kComponentCount);

#elif TESSERA_BENCH_IMPL == TESSERA_BENCH_MOSAIC || TESSERA_BENCH_IMPL == TESSERA_BENCH_TUPLE

template<class Sequence>
struct make_system;

template<std::size_t... Is>
struct make_system<std::index_sequence<Is...>> {
#  if TESSERA_BENCH_IMPL == TESSERA_BENCH_MOSAIC
    using type = tessera::mosaic<Component<Is>...>;
#  else
    using type = std::tuple<Component<Is>...>;
#  endif
};

using System = typename make_system<std::make_index_sequence<kComponentCount>>::type;

#endif

}  // namespace

int main() {
#if TESSERA_BENCH_IMPL == TESSERA_BENCH_BASELINE
    int sum = 0;
#elif TESSERA_BENCH_IMPL == TESSERA_BENCH_TUPLE
    System system;
    int sum = 0;
    std::apply([&](auto&... components) { ((sum += components.value), ...); }, system);
#else
    System system;
    int sum = 0;
    system.for_each([&](const auto& component) { sum += component.value; });
#endif
    std::printf("%d\n", sum);
    return 0;
}
