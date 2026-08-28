// Copyright 2026 Denis Tishkov
// SPDX-License-Identifier: Apache-2.0

#ifndef TESSERA_DEDUP_HPP
#define TESSERA_DEDUP_HPP

#include <cstddef>
#include <type_traits>
#include <utility>

#include "tessera/config.hpp"

#if TESSERA_DEDUP_BACKEND == TESSERA_DEDUP_REFLECTION && !TESSERA_HAS_REFLECTION
#  error "TESSERA_DEDUP_BACKEND=REFLECTION, but this compiler has no static reflection."
#endif

#if TESSERA_HAS_REFLECTION
#  include TESSERA_REFLECTION_HEADER
#  include <vector>
#endif

/// @file dedup.hpp
/// @brief The interchangeable implementations of "remove duplicate types from a pack".
///
/// Deduplication is the hot spot of the whole library: assembling a system out of N components
/// that each declare their dependencies means deduplicating a list whose length grows with N.
/// Four implementations live here, all producing the identical type — the first occurrence of
/// each type survives, in its original position — and differing only in what they cost the
/// compiler (see docs/benchmarks.md):
///
///   * `TESSERA_DEDUP_PORTABLE`   divide and conquer, the default anywhere;
///   * `TESSERA_DEDUP_BUILTIN`    one Clang builtin, no library recursion at all;
///   * `TESSERA_DEDUP_REFLECTION` P2996 reflection, an experimental sketch;
///   * `TESSERA_DEDUP_FOLD`       the textbook linear fold, kept as the benchmark reference.

namespace tessera {

template<class... Ts>
struct type_list;

namespace detail {

template<class... Ls>
struct concat;  // defined in type_list.hpp

// ---------------------------------------------------------------------------------------------
// Pack indexing, needed to split a list in half.
//
// The portable form maps every index to its type through one class that inherits from N tagged
// bases, then lets overload resolution pick the base whose index matches: constant instantiation
// depth, one indexer per list, no linear walk.
// ---------------------------------------------------------------------------------------------

template<std::size_t I, class T>
struct indexed_type {};

template<class Sequence, class... Ts>
struct type_indexer;

template<std::size_t... Is, class... Ts>
struct type_indexer<std::index_sequence<Is...>, Ts...> : indexed_type<Is, Ts>... {};

template<std::size_t I, class T>
std::type_identity<T> select_indexed(const indexed_type<I, T>&);  // declared, never defined

template<std::size_t I, class... Ts>
using pack_element_portable_t =
    typename decltype(select_indexed<I>(type_indexer<std::index_sequence_for<Ts...>, Ts...>{}))::type;

#if TESSERA_HAS_TYPE_PACK_ELEMENT
template<std::size_t I, class... Ts>
using pack_element_t = __type_pack_element<I, Ts...>;
#else
template<std::size_t I, class... Ts>
using pack_element_t = pack_element_portable_t<I, Ts...>;
#endif

/// `Length` elements of @p L starting at @p Offset.
template<class L, std::size_t Offset, class Sequence>
struct slice;

template<class... Ts, std::size_t Offset, std::size_t... Is>
struct slice<type_list<Ts...>, Offset, std::index_sequence<Is...>> {
    using type = type_list<pack_element_t<Offset + Is, Ts...>...>;
};

template<class L, std::size_t Offset, std::size_t Length>
using slice_t = typename slice<L, Offset, std::make_index_sequence<Length>>::type;

// ---------------------------------------------------------------------------------------------
// Backend FOLD — the obvious implementation, and the one to beat.
//
// A left fold that appends a type only when the accumulator does not hold it yet. Correct, four
// lines long, and quadratic in both instantiations and retained types: every step creates a new
// type_list specialization one element longer than the last.
// ---------------------------------------------------------------------------------------------

template<class Acc, class... Ts>
struct unique_fold;

template<class Acc>
struct unique_fold<Acc> {
    using type = Acc;
};

template<class... Kept, class Head, class... Tail>
struct unique_fold<type_list<Kept...>, Head, Tail...>
    : unique_fold<std::conditional_t<(std::is_same_v<Head, Kept> || ...), type_list<Kept...>, type_list<Kept..., Head>>,
                  Tail...> {};

template<class... Ts>
using unique_fold_t = typename unique_fold<type_list<>, Ts...>::type;

template<class L>
struct unique_fold_of;

template<class... Ts>
struct unique_fold_of<type_list<Ts...>> {
    using type = unique_fold_t<Ts...>;
};

// ---------------------------------------------------------------------------------------------
// Backend PORTABLE — a hybrid, and the default everywhere.
//
// Short lists go to the fold above, which is hard to beat on constant factors. Longer ones are
// halved, each half deduplicated, and the results merged: instantiation depth stays bounded by the
// threshold plus a logarithmic number of merges, where the plain fold — one level per element —
// would run past the compiler's 1024-deep limit at a few hundred components.
// ---------------------------------------------------------------------------------------------

/// Membership testing through the base-class table.
///
/// Comparing a candidate against a list with `is_same` costs one instantiation per element, so a
/// merge costs |A| * |B|. Instead the already-unique left half is turned into a class that
/// inherits one tag per element: `is_base_of` then answers membership with a single base-class
/// lookup, which the compiler resolves through the table it built once. A merge costs |B| tests
/// rather than |A| * |B| comparisons, which is what makes the divide-and-conquer backend scale.
///
/// The construction is only valid because @p Ts is already unique — repeating a base class is
/// ill-formed, and that is exactly the invariant the algorithm maintains.
template<class T>
struct type_tag {};

template<class... Ts>
struct type_set : type_tag<Ts>... {};

template<class T, class Set>
inline constexpr bool is_member_of = std::is_base_of_v<type_tag<T>, Set>;

/// Appends the elements of @p B that do not already occur in the (already unique) list @p A.
template<class A, class B>
struct merge_unique;

template<class... As, class... Bs>
struct merge_unique<type_list<As...>, type_list<Bs...>> {
    using set = type_set<As...>;
    using type = typename concat<type_list<As...>,
                                 std::conditional_t<is_member_of<Bs, set>, type_list<>, type_list<Bs>>...>::type;
};

template<class... Bs>
struct merge_unique<type_list<>, type_list<Bs...>> {
    using type = type_list<Bs...>;
};

/// Below this length the linear fold wins: it allocates no indexer, slices nothing and keeps the
/// constant factor low. Above it the fold's instantiation depth — one level per element — would
/// approach the compiler's limit (1024 by default), so the list is halved first. Measured on the
/// benchmark in docs/benchmarks.md; see docs/design.md for why the crossover sits here.
inline constexpr std::size_t fold_threshold = 256;

template<class L, std::size_t Size = L::size>
struct unique_divide {
    static constexpr std::size_t half = Size / 2;

    using left = typename unique_divide<slice_t<L, 0, half>>::type;
    using right = typename unique_divide<slice_t<L, half, Size - half>>::type;
    using type = typename merge_unique<left, right>::type;
};

template<class L, std::size_t Size>
    requires(Size <= fold_threshold)
struct unique_divide<L, Size> {
    using type = typename unique_fold_of<L>::type;
};

template<class... Ts>
using unique_portable_t = typename unique_divide<type_list<Ts...>>::type;

// ---------------------------------------------------------------------------------------------
// Backend BUILTIN — Clang 22+.
//
// `__builtin_dedup_pack<Ts...>` expands to the same pack with duplicates dropped, in one shot:
// no accumulator, no recursion, no intermediate specializations for the compiler to retain.
// ---------------------------------------------------------------------------------------------

#if TESSERA_HAS_BUILTIN_DEDUP_PACK
template<class... Ts>
using unique_builtin_t = type_list<__builtin_dedup_pack<Ts...>...>;
#endif

// ---------------------------------------------------------------------------------------------
// Backend REFLECTION — P2996 (experimental).
//
// With reflection a type list becomes ordinary constexpr data: a `std::vector<std::meta::info>`
// filtered by a plain loop and spliced back into a specialization. This is the seam the whole
// library is layered around — when a mainstream toolchain ships P2996, this backend replaces the
// template machinery without a single change to type_list.hpp or mosaic.hpp.
//
// Compiled whenever the compiler offers reflection — not only when this backend is selected — so
// that dedup_backends.test.cpp can assert it produces the same types as the template path.
//
// The loop is quadratic in ordinary constexpr code rather than in template instantiations, which is
// the whole point; the price is that long lists can exhaust the compiler's constant-evaluation
// budget. Clang needs `-fconstexpr-steps` raised (a 1200-element list needs roughly 2e8) or it
// reports the splice operand as "not a constant expression". See docs/reflection.md.
// ---------------------------------------------------------------------------------------------

#if TESSERA_HAS_REFLECTION
consteval auto unique_metas(std::vector<std::meta::info> metas) -> std::vector<std::meta::info> {
    std::vector<std::meta::info> kept;
    for (const std::meta::info meta : metas) {
        bool seen = false;
        for (const std::meta::info already : kept) {
            if (already == meta) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            kept.push_back(meta);
        }
    }
    return kept;
}

/// The substitution is named rather than spliced inline: a splice operand has to be a constant
/// expression, and a `consteval` call written directly inside `[: :]` is not one — the result has to
/// be produced by a function (or a constexpr variable) first.
template<class... Ts>
consteval std::meta::info unique_reflection_info() {
    return std::meta::substitute(^^type_list, unique_metas({^^Ts...}));
}

template<class... Ts>
using unique_reflection_t = [:unique_reflection_info<Ts...>():];
#endif

// ---------------------------------------------------------------------------------------------
// The active backend.
// ---------------------------------------------------------------------------------------------

template<class... Ts>
using deduplicated_t =
#if TESSERA_DEDUP_BACKEND == TESSERA_DEDUP_BUILTIN
    unique_builtin_t<Ts...>;
#elif TESSERA_DEDUP_BACKEND == TESSERA_DEDUP_REFLECTION
    unique_reflection_t<Ts...>;
#elif TESSERA_DEDUP_BACKEND == TESSERA_DEDUP_FOLD
    unique_fold_t<Ts...>;
#else
    unique_portable_t<Ts...>;
#endif

}  // namespace detail

/// @brief Name of the deduplication backend selected for this translation unit.
inline constexpr const char* dedup_backend_name = TESSERA_DEDUP_BACKEND_NAME;

}  // namespace tessera

#endif  // TESSERA_DEDUP_HPP
