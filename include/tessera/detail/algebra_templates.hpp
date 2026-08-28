// Copyright 2026 Denis Tishkov
// SPDX-License-Identifier: Apache-2.0

#ifndef TESSERA_DETAIL_ALGEBRA_TEMPLATES_HPP
#define TESSERA_DETAIL_ALGEBRA_TEMPLATES_HPP

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>

#include "tessera/config.hpp"

/// @file algebra_templates.hpp
/// @brief The list algebra implemented with template metaprogramming — the implementation that
///        works on every C++23 compiler.
///
/// Five operations make up the seam between the public types and their implementation:
///
/// | operation            | meaning                                                          |
/// |----------------------|------------------------------------------------------------------|
/// | `unique_into`        | drop duplicates, feed the survivors to a target template         |
/// | `splice_unique_into` | splice nested lists, drop duplicates, feed the target            |
/// | `concat_into`        | concatenate lists, keep duplicates, feed the target              |
/// | `select_into`        | keep the elements whose mask bit is set, feed the target         |
/// | `nth`                | the I-th element                                                 |
///
/// Each one takes the target template directly, so that building a `mosaic` does not first build a
/// `type_list` that nothing else will ever look at. `algebra_reflection.hpp` implements the same
/// five with static reflection; `tessera::detail::ops` names whichever is active.

namespace tessera {

template<class... Ts>
struct type_list;

namespace detail::tmpl {

// -- pack indexing -------------------------------------------------------------------------------
//
// The portable form maps every index to its type through one class that inherits from N tagged
// bases, then lets overload resolution pick the base whose index matches: constant instantiation
// depth, one indexer per list, no linear walk.

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

// -- concatenation -------------------------------------------------------------------------------
//
// Written the textbook way — fold two lists at a time — concatenation costs one intermediate
// specialization per input list, each one element longer than the last: quadratic in retained types
// and linear in instantiation depth. Instead a consteval loop computes, for every element of the
// result, which input list it comes from and at which position, and one pack expansion over that
// plan produces the answer.

template<class L, std::size_t I>
struct list_element;

template<class... Ts, std::size_t I>
struct list_element<type_list<Ts...>, I> {
    using type = pack_element_t<I, Ts...>;
};

template<template<class...> class Target, class... Ls>
struct concat_into_impl {
private:
    static constexpr std::size_t total = (std::size_t{0} + ... + Ls::size);

    static constexpr auto plan = [] {
        std::array<std::size_t, total> fromList{};
        std::array<std::size_t, total> fromPosition{};
        const std::array<std::size_t, sizeof...(Ls)> sizes{Ls::size...};
        std::size_t out = 0;
        for (std::size_t list = 0; list < sizes.size(); ++list) {
            for (std::size_t position = 0; position < sizes[list]; ++position) {
                fromList[out] = list;
                fromPosition[out] = position;
                ++out;
            }
        }
        return std::pair{fromList, fromPosition};
    }();

    template<std::size_t... Is>
    static auto build(std::index_sequence<Is...>)
        -> Target<typename list_element<pack_element_t<plan.first[Is], Ls...>, plan.second[Is]>::type...>;

public:
    using type = decltype(build(std::make_index_sequence<total>{}));
};

template<template<class...> class Target>
struct concat_into_impl<Target> {
    using type = Target<>;
};

// -- deduplication: FOLD ---------------------------------------------------------------------------
//
// A left fold that appends a type only when the accumulator does not hold it yet. Correct, four
// lines long, and quadratic in both instantiations and retained types: every step creates a new
// list specialization one element longer than the last. It is also one instantiation level per
// element, so it runs past the compiler's 1024-deep limit at a few hundred components.

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

// -- deduplication: PORTABLE (hybrid) --------------------------------------------------------------
//
// Short lists go to the fold above, which is hard to beat on constant factors. Longer ones are
// halved, each half deduplicated, and the results merged, so instantiation depth stays bounded by
// the threshold plus a logarithmic number of merges.
//
// The merge is where the second trick lives. Comparing a candidate against the left half with
// `is_same` would cost |A| * |B| instantiations; instead the left half — already unique — becomes a
// class that inherits one tag per element, and `is_base_of` answers membership from the table the
// compiler built once. That construction is only well-formed because the left half is unique, which
// is exactly the invariant the algorithm maintains.

template<class T>
struct type_tag {};

template<class... Ts>
struct type_set : type_tag<Ts>... {};

template<class T, class Set>
inline constexpr bool is_member_of = std::is_base_of_v<type_tag<T>, Set>;

template<class L, std::size_t Offset, class Sequence>
struct slice;

template<class... Ts, std::size_t Offset, std::size_t... Is>
struct slice<type_list<Ts...>, Offset, std::index_sequence<Is...>> {
    using type = type_list<pack_element_t<Offset + Is, Ts...>...>;
};

template<class L, std::size_t Offset, std::size_t Length>
using slice_t = typename slice<L, Offset, std::make_index_sequence<Length>>::type;

template<class A, class B>
struct merge_unique;

template<class... As, class... Bs>
struct merge_unique<type_list<As...>, type_list<Bs...>> {
    using set = type_set<As...>;
    using type =
        typename concat_into_impl<type_list, type_list<As...>,
                                  std::conditional_t<is_member_of<Bs, set>, type_list<>, type_list<Bs>>...>::type;
};

template<class... Bs>
struct merge_unique<type_list<>, type_list<Bs...>> {
    using type = type_list<Bs...>;
};

/// Below this length the fold wins on constant factors; above it the list is halved first, so that
/// instantiation depth never approaches the compiler's limit. Measured in docs/benchmarks.md.
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

// -- deduplication: BUILTIN ------------------------------------------------------------------------

#if TESSERA_HAS_BUILTIN_DEDUP_PACK
template<class... Ts>
using unique_builtin_t = type_list<__builtin_dedup_pack<Ts...>...>;
#endif

// -- the selected deduplication primitive ----------------------------------------------------------

template<class... Ts>
using deduplicated_t =
#if TESSERA_ALGEBRA_BACKEND == TESSERA_ALGEBRA_BUILTIN
    unique_builtin_t<Ts...>;
#elif TESSERA_ALGEBRA_BACKEND == TESSERA_ALGEBRA_FOLD
    unique_fold_t<Ts...>;
#else
    unique_portable_t<Ts...>;
#endif

// -- the five operations ---------------------------------------------------------------------------

template<template<class...> class Target, class... Ts>
struct unique_into_impl {
    using type = typename deduplicated_t<Ts...>::template into<Target>;
};

template<template<class...> class Target, class L>
struct unique_list_into;

template<template<class...> class Target, class... Ts>
struct unique_list_into<Target, type_list<Ts...>> {
    using type = typename unique_into_impl<Target, Ts...>::type;
};

/// Every argument is a `type_list`; their elements are concatenated and deduplicated.
template<template<class...> class Target, class... Ls>
struct splice_unique_into_impl {
    using type = typename unique_list_into<Target, typename concat_into_impl<type_list, Ls...>::type>::type;
};

/// One list needs no concatenation.
template<template<class...> class Target, class... Ts>
struct splice_unique_into_impl<Target, type_list<Ts...>> {
    using type = typename unique_into_impl<Target, Ts...>::type;
};

/// Keeps the elements whose mask bit is set, in order.
template<template<class...> class Target, class Mask, class... Ts>
struct select_into_impl;

template<template<class...> class Target, bool... Keep, class... Ts>
struct select_into_impl<Target, std::integer_sequence<bool, Keep...>, Ts...> {
    using type = typename concat_into_impl<Target, std::conditional_t<Keep, type_list<Ts>, type_list<>>...>::type;
};

template<std::size_t I, class... Ts>
struct nth_impl {
    static_assert(I < sizeof...(Ts), "tessera::type_list::nth — index out of range");
    using type = pack_element_t<I, Ts...>;
};

// -- the seam: five alias templates, the same names the reflection implementation exports -----------

template<template<class...> class Target, class... Ts>
using unique_into = typename unique_into_impl<Target, Ts...>::type;

template<template<class...> class Target, class... Ls>
using splice_unique_into = typename splice_unique_into_impl<Target, Ls...>::type;

template<template<class...> class Target, class... Ls>
using concat_into = typename concat_into_impl<Target, Ls...>::type;

template<template<class...> class Target, class Mask, class... Ts>
using select_into = typename select_into_impl<Target, Mask, Ts...>::type;

template<std::size_t I, class... Ts>
using nth = typename nth_impl<I, Ts...>::type;

}  // namespace detail::tmpl
}  // namespace tessera

#endif  // TESSERA_DETAIL_ALGEBRA_TEMPLATES_HPP
