// Copyright 2026 Denis Tishkov
// SPDX-License-Identifier: Apache-2.0
//
// The translation unit measured by run_compile_bench.py. It is compiled repeatedly with a
// different component count and a different implementation, and the compiler's wall time and peak
// resident memory are recorded.
//
//   -DTESSERA_BENCH_N=<count>   how many distinct component types the system ends up with
//   -DTESSERA_BENCH_IMPL=<id>   0 baseline | 1 std::tuple | 2 mosaic | 3 assembly | 4 algebra
//                               | 5 setup | 6 resolve
//
// 0 BASELINE  — headers only, so the fixed cost of the toolchain can be subtracted.
// 1 TUPLE     — N components in a std::tuple, addressed by type with std::get<T>.
// 2 MOSAIC    — the same N components in a tessera::mosaic, addressed with get<T>.
//               (1) and (2) together isolate the cost of the container itself.
// 3 ASSEMBLY  — N systems, each declaring four overlapping dependencies: 4N types are
//               flat-mapped and deduplicated down to N, then assembled into a mosaic. This is
//               the realistic workload, and it is bounded by the container: a mosaic of N
//               elements is N base classes and N accessor instantiations.
// 4 ALGEBRA   — the deduplication *alone*: the same 4N type mentions, deduplicated into a
//               `type_list`, with no container built on top and no per-system indirection.
//               Nothing is instantiated but the N component types themselves, so this is the
//               implementation that can be pushed to N far beyond what a container survives —
//               which is what makes the two algebra implementations comparable at scale.
// 5 SETUP     — the same 4N mentions collected into a `type_list` with *no* deduplication: the
//               floor under (4). Instantiating N distinct class templates and carrying a 4N-element
//               argument pack costs what it costs on every algebra implementation, so
//               (4) minus (5) is the price of deduplication and nothing else.
// 6 RESOLVE   — N services in a DAG (each depending on three smaller ones), resolved transitively
//               and topologically sorted by `tessera::resolve`. Against (3) this is what following
//               the graph costs over merely flattening a list that was already complete.

#include <cstddef>
#include <cstdio>
#include <utility>

#define TESSERA_BENCH_BASELINE 0
#define TESSERA_BENCH_TUPLE 1
#define TESSERA_BENCH_MOSAIC 2
#define TESSERA_BENCH_ASSEMBLY 3
#define TESSERA_BENCH_ALGEBRA 4
#define TESSERA_BENCH_SETUP 5
#define TESSERA_BENCH_RESOLVE 6

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

/// The dependency pattern shared by the ASSEMBLY and ALGEBRA implementations: system `s` declares
/// components `s`, `7s+1`, `13s+5` and `s+1` (mod N), so 4N mentions collapse to exactly N
/// distinct types and the duplicates are scattered rather than adjacent.
constexpr std::size_t kDependenciesPerSystem = 4;

consteval std::size_t dependency_index(std::size_t mention) noexcept {
    const std::size_t system = mention / kDependenciesPerSystem;
    switch (mention % kDependenciesPerSystem) {
        case 0: return system % kComponentCount;
        case 1: return (system * 7 + 1) % kComponentCount;
        case 2: return (system * 13 + 5) % kComponentCount;
        default: return (system + 1) % kComponentCount;
    }
}

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
using Assembled = Systems::flat_map<dependencies_of>::into<tessera::mosaic>;
static_assert(Assembled::size == kComponentCount);

#elif TESSERA_BENCH_IMPL == TESSERA_BENCH_ALGEBRA

/// The five-operation seam, called directly. `unique_into` is the primitive that
/// `splice_unique_into` — and therefore `tessera::of` and `flat_map` — is built on; feeding it one
/// flat pack instead of N four-element lists removes the N `type_list` specializations that only
/// exist to set the benchmark up, leaving deduplication as the only thing being measured.
template<class Sequence>
struct make_algebra;

template<std::size_t... Is>
struct make_algebra<std::index_sequence<Is...>> {
    using type = tessera::detail::ops::unique_into<tessera::type_list, Component<dependency_index(Is)>...>;
};

using Deduplicated =
    typename make_algebra<std::make_index_sequence<kDependenciesPerSystem * kComponentCount>>::type;
static_assert(Deduplicated::size == kComponentCount);

#elif TESSERA_BENCH_IMPL == TESSERA_BENCH_RESOLVE

/// Service `I` depends on `I/2`, `I/3` and `I/5`, all strictly smaller, so the graph is acyclic,
/// about `3N` edges deep in `log N` levels, and most nodes are reachable by several paths — which
/// is what makes the "have I emitted this already" check do real work.
template<std::size_t I>
struct Service {
    using dependencies = tessera::type_list<Service<I / 2>, Service<I / 3>, Service<I / 5>>;
};

template<>
struct Service<0> {};

template<class Sequence>
struct make_services;

template<std::size_t... Is>
struct make_services<std::index_sequence<Is...>> {
    using type = tessera::resolved_t<Service<Is>...>;
};

using Resolved = typename make_services<std::make_index_sequence<kComponentCount>>::type;
static_assert(Resolved::size == kComponentCount);
static_assert(tessera::is_topologically_sorted<Resolved>);

#elif TESSERA_BENCH_IMPL == TESSERA_BENCH_SETUP

/// Everything the ALGEBRA implementation does *except* deduplicating: the N component types are
/// instantiated and the 4N-element pack is carried into a `type_list`, which is one specialization.
template<class Sequence>
struct make_setup;

template<std::size_t... Is>
struct make_setup<std::index_sequence<Is...>> {
    using type = tessera::type_list<Component<dependency_index(Is)>...>;
};

using Mentioned = typename make_setup<std::make_index_sequence<kDependenciesPerSystem * kComponentCount>>::type;
static_assert(Mentioned::size == kDependenciesPerSystem * kComponentCount);

#elif TESSERA_BENCH_IMPL == TESSERA_BENCH_MOSAIC || TESSERA_BENCH_IMPL == TESSERA_BENCH_TUPLE

template<class Sequence>
struct make_container;

template<std::size_t... Is>
struct make_container<std::index_sequence<Is...>> {
#  if TESSERA_BENCH_IMPL == TESSERA_BENCH_MOSAIC
    using type = tessera::mosaic<Component<Is>...>;
#  else
    using type = std::tuple<Component<Is>...>;
#  endif
};

using Assembled = typename make_container<std::make_index_sequence<kComponentCount>>::type;

#endif

}  // namespace

int main() {
#if TESSERA_BENCH_IMPL == TESSERA_BENCH_BASELINE || TESSERA_BENCH_IMPL == TESSERA_BENCH_ALGEBRA || \
    TESSERA_BENCH_IMPL == TESSERA_BENCH_SETUP || TESSERA_BENCH_IMPL == TESSERA_BENCH_RESOLVE
    int sum = 0;
#elif TESSERA_BENCH_IMPL == TESSERA_BENCH_TUPLE
    Assembled container;
    int sum = 0;
    std::apply([&](auto&... components) { ((sum += components.value), ...); }, container);
#else
    Assembled container;
    int sum = 0;
    container.for_each([&](const auto& component) { sum += component.value; });
#endif
    std::printf("%d\n", sum);
    return 0;
}
