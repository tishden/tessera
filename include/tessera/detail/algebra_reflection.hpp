// Copyright 2026 Denis Tishkov
// SPDX-License-Identifier: Apache-2.0

#ifndef TESSERA_DETAIL_ALGEBRA_REFLECTION_HPP
#define TESSERA_DETAIL_ALGEBRA_REFLECTION_HPP

#include "tessera/config.hpp"

#if TESSERA_HAS_REFLECTION

#  include <array>
#  include <cstddef>
#  include <utility>
#  include <vector>

#  include TESSERA_REFLECTION_HEADER

/// @file algebra_reflection.hpp
/// @brief The same five operations as algebra_templates.hpp, implemented with static reflection.
///
/// The template implementation computes with types: every intermediate result is a class
/// specialization the compiler creates, names, and keeps. This one computes with *values* — a
/// `std::vector<std::meta::info>` inside one `consteval` function — and produces exactly one type at
/// the end, the answer. Nothing intermediate exists to be retained.
///
/// Two rules shape the code below, both learned the hard way (docs/reflection.md):
///
///  * a splice operand must be a constant expression, and an expression that still owns a
///    `std::vector` is not one — so every helper builds its vector *inside* the function and returns
///    a bare `std::meta::info`;
///  * the operations take the target template directly, so assembling a `mosaic` produces a mosaic
///    rather than a `type_list` that is then re-substituted.

namespace tessera {

template<class... Ts>
struct type_list;

namespace detail::refl {

/// Appends @p meta unless the vector already holds it.
consteval void push_unique(std::vector<std::meta::info>& kept, std::meta::info meta) {
    for (const std::meta::info already : kept) {
        if (already == meta) {
            return;
        }
    }
    kept.push_back(meta);
}

// -- the five operations ---------------------------------------------------------------------------

template<template<class...> class Target, class... Ts>
consteval std::meta::info unique_into_info() {
    std::vector<std::meta::info> kept;
    kept.reserve(sizeof...(Ts));
    (push_unique(kept, ^^Ts), ...);
    return std::meta::substitute(^^Target, kept);
}

template<template<class...> class Target, class... Ts>
using unique_into = [:unique_into_info<Target, Ts...>():];

/// Every argument is a `type_list`; its elements are spliced in and deduplicated in one pass — the
/// operation the whole library is built on, and the one that used to cost three layers of
/// intermediate specializations.
template<template<class...> class Target, class... Ls>
consteval std::meta::info splice_unique_into_info() {
    std::vector<std::meta::info> kept;
    const auto splice = [&kept](std::meta::info list) {
        for (const std::meta::info element : std::meta::template_arguments_of(list)) {
            push_unique(kept, element);
        }
    };
    (splice(^^Ls), ...);
    return std::meta::substitute(^^Target, kept);
}

template<template<class...> class Target, class... Ls>
using splice_unique_into = [:splice_unique_into_info<Target, Ls...>():];

/// Concatenation without deduplication.
template<template<class...> class Target, class... Ls>
consteval std::meta::info concat_into_info() {
    std::vector<std::meta::info> all;
    const auto append = [&all](std::meta::info list) {
        for (const std::meta::info element : std::meta::template_arguments_of(list)) {
            all.push_back(element);
        }
    };
    (append(^^Ls), ...);
    return std::meta::substitute(^^Target, all);
}

template<template<class...> class Target, class... Ls>
using concat_into = [:concat_into_info<Target, Ls...>():];

/// Keeps the elements whose mask bit is set. The mask arrives as a type so that the two packs — the
/// bits and the types — can live in different scopes.
template<class Mask>
struct with_mask;

template<bool... Keep>
struct with_mask<std::integer_sequence<bool, Keep...>> {
    template<template<class...> class Target, class... Ts>
    static consteval std::meta::info select() {
        static_assert(sizeof...(Keep) == sizeof...(Ts), "mask and list have different lengths");
        std::vector<std::meta::info> all;
        all.reserve(sizeof...(Ts));
        (all.push_back(^^Ts), ...);

        // std::array rather than std::vector<bool>: no proxy references, and a zero-length mask is
        // still a valid object.
        const std::array<bool, sizeof...(Keep)> keep{Keep...};

        std::vector<std::meta::info> kept;
        for (std::size_t i = 0; i < all.size(); ++i) {
            if (keep[i]) {
                kept.push_back(all[i]);
            }
        }
        return std::meta::substitute(^^Target, kept);
    }
};

template<template<class...> class Target, class Mask, class... Ts>
using select_into = [:with_mask<Mask>::template select<Target, Ts...>():];

template<std::size_t I, class... Ts>
consteval std::meta::info nth_info() {
    std::vector<std::meta::info> all;
    all.reserve(sizeof...(Ts));
    (all.push_back(^^Ts), ...);
    return all[I];
}

template<std::size_t I, class... Ts>
using nth = [:nth_info<I, Ts...>():];

}  // namespace detail::refl
}  // namespace tessera

#endif  // TESSERA_HAS_REFLECTION
#endif  // TESSERA_DETAIL_ALGEBRA_REFLECTION_HPP
