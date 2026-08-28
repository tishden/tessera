// Copyright 2026 Denis Tishkov
// SPDX-License-Identifier: Apache-2.0

#ifndef TESSERA_TYPE_LIST_HPP
#define TESSERA_TYPE_LIST_HPP

#include <cstddef>
#include <type_traits>
#include <utility>

#include "tessera/algebra.hpp"
#include "tessera/config.hpp"

/// @file type_list.hpp
/// @brief `tessera::type_list` — the value-less half of the library: a compile-time list of types
///        and the algebra used to build one.
///
/// `type_list` carries no storage and is never instantiated as an object. `tessera::mosaic`
/// (see mosaic.hpp) is the storage-bearing counterpart, and both express every operation in terms
/// of the same five primitives in `tessera::detail::ops` — which is what lets the whole algebra be
/// swapped for a static-reflection implementation without touching either type.

namespace tessera {

/// @brief Sentinel returned by `type_list::index_of` when the type is not a member.
inline constexpr std::size_t npos = static_cast<std::size_t>(-1);

namespace detail {

template<class T, class... Ts>
consteval std::size_t index_of_impl() noexcept {
    std::size_t index = 0;
    std::size_t found = npos;
    (((found == npos && std::is_same_v<T, Ts>) ? (found = index, ++index) : ++index), ...);
    return found;
}

template<class L>
struct unique_of;

}  // namespace detail

/// @brief Customization point, declared here because `type_list::flat_map` needs it; defined below.
template<class T>
struct elements_of;

/// @brief An ordered, compile-time list of types. Duplicates are allowed; `tessera::unique_t`
///        removes them.
///
/// @code
/// using L = tessera::type_list<int, double, int>;
/// static_assert(L::size == 3);
/// static_assert(tessera::unique_t<L>::size == 2);
/// static_assert(L::contains<double>);
/// @endcode
template<class... Ts>
struct type_list {
    /// Marks the type as spliceable by `tessera::of` / `flat_map` (see `tessera::elements_of`).
    using tessera_elements = type_list<Ts...>;

    static constexpr std::size_t size = sizeof...(Ts);
    static constexpr bool empty = (sizeof...(Ts) == 0);

    /// @brief Whether @p T occurs in the list. A fold, on both implementations: there is nothing
    ///        for reflection to improve on a single expression that creates no types.
    template<class T>
    static constexpr bool contains = (std::is_same_v<T, Ts> || ...);

    /// @brief Position of the first occurrence of @p T, or `tessera::npos`.
    template<class T>
    static constexpr std::size_t index_of = detail::index_of_impl<T, Ts...>();

    /// @brief The @p I-th element.
    template<std::size_t I>
    using nth = detail::ops::nth<I, Ts...>;

    /// @brief Feeds the element types to another template: `into<std::tuple>` → `std::tuple<Ts...>`.
    template<template<class...> class Target>
    using into = Target<Ts...>;

    /// @brief The same list with @p Us appended (no deduplication — see `tessera::unique_t`).
    template<class... Us>
    using append = type_list<Ts..., Us...>;

    /// @brief Concatenation with another `type_list`.
    template<class Other>
    using concat = detail::ops::concat_into<type_list, type_list<Ts...>, Other>;

    // Deduplication and flattening are deliberately *not* members: a non-template member alias is
    // instantiated together with its class, so `using unique = ...` would deduplicate every list the
    // moment it is named, whether anyone asked for it or not. They live next door as
    // `tessera::unique_t<L>` and `tessera::flatten_t<Ts...>`, which are instantiated on use.

    /// @brief Elements for which `Predicate<T>::value` is `true`, order preserved.
    ///
    /// The predicate is a template, so the compiler evaluates it once per element either way; only
    /// the list surgery moves to the algebra, as a bit mask.
    template<template<class...> class Predicate>
    using filter =
        detail::ops::select_into<type_list, std::integer_sequence<bool, static_cast<bool>(Predicate<Ts>::value)...>,
                                 Ts...>;

    /// @brief `Function<T>` for every element, deduplicated (a mapping may collapse types).
    template<template<class...> class Function>
    using transform = detail::ops::unique_into<type_list, Function<Ts>...>;

    /// @brief `Function<T>` for every element, with list-like results spliced in, deduplicated.
    ///
    /// This is how a system collects the union of its components' dependencies: each component
    /// answers with a list, and the lists are merged into one flat set of distinct types.
    template<template<class...> class Function>
    using flat_map = detail::ops::splice_unique_into<type_list, typename elements_of<Function<Ts>>::type...>;
};

/// @brief How a type contributes to a flattened list (the customization point declared above).
///
/// The primary template treats a type as a single leaf. Anything exposing a `tessera_elements`
/// member list (`type_list`, `mosaic`) is spliced in, recursively. Specialize it to teach
/// `tessera::of<...>` about your own list type:
///
/// @code
/// template<class... Ts>
/// struct tessera::elements_of<std::tuple<Ts...>> { using type = tessera::type_list<Ts...>; };
/// @endcode
template<class T>
struct elements_of {
    using type = type_list<T>;
};

template<class... Ts>
struct elements_of<type_list<Ts...>> {
    using type = detail::ops::concat_into<type_list, typename elements_of<Ts>::type...>;
};

template<class T>
    requires requires { typename T::tessera_elements; }
struct elements_of<T> {
    using type = typename elements_of<typename T::tessera_elements>::type;
};

template<class T>
using elements_of_t = typename elements_of<T>::type;

/// @brief Concatenation of any number of `type_list`s.
template<class... Ls>
using concat_t = detail::ops::concat_into<type_list, Ls...>;

/// @brief A `type_list` with duplicates removed, using the active algebra implementation.
template<class L>
using unique_t = typename detail::unique_of<L>::type;

/// @brief The flat, distinct union of @p Ts — leaves stay, list-like types are spliced in.
///
/// The type-level counterpart of `tessera::of<...>`.
template<class... Ts>
using flatten_t = detail::ops::splice_unique_into<type_list, elements_of_t<Ts>...>;

namespace detail {

template<class... Ts>
struct unique_of<type_list<Ts...>> {
    using type = ops::unique_into<type_list, Ts...>;
};

}  // namespace detail
}  // namespace tessera

#endif  // TESSERA_TYPE_LIST_HPP
