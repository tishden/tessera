// Copyright 2026 Denis Tishkov
// SPDX-License-Identifier: Apache-2.0

#ifndef TESSERA_TYPE_LIST_HPP
#define TESSERA_TYPE_LIST_HPP

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>

#include "tessera/config.hpp"
#include "tessera/dedup.hpp"

/// @file type_list.hpp
/// @brief `tessera::type_list` — the value-less half of the library: a compile-time list of types
///        plus the algebra used to build one (concat, unique, filter, transform, flat_map).
///
/// `type_list` carries no storage and is never instantiated as an object. `tessera::mosaic`
/// (see mosaic.hpp) is the storage-bearing counterpart and delegates every type-level operation
/// here, so the algebra can be tested — and reasoned about — on its own.

namespace tessera {

/// @brief Sentinel returned by `type_list::index_of` when the type is not a member.
inline constexpr std::size_t npos = static_cast<std::size_t>(-1);

namespace detail {

template<class... Ls>
struct concat;

template<class L>
struct unique_of;

template<class L, template<class...> class Predicate>
struct filter_impl;

template<class L, template<class...> class Function>
struct transform_impl;

template<class L, template<class...> class Function>
struct flat_map_impl;

template<class L>
struct flatten_impl;

template<std::size_t I, class... Ts>
struct nth_impl;

template<class T, class... Ts>
consteval std::size_t index_of_impl() noexcept {
    std::size_t index = 0;
    std::size_t found = npos;
    (((found == npos && std::is_same_v<T, Ts>) ? (found = index, ++index) : ++index), ...);
    return found;
}

}  // namespace detail

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

    /// @brief Whether @p T occurs in the list.
    template<class T>
    static constexpr bool contains = (std::is_same_v<T, Ts> || ...);

    /// @brief Position of the first occurrence of @p T, or `tessera::npos`.
    template<class T>
    static constexpr std::size_t index_of = detail::index_of_impl<T, Ts...>();

    /// @brief The @p I-th element. O(1) where the compiler offers `__type_pack_element`.
    template<std::size_t I>
    using nth = typename detail::nth_impl<I, Ts...>::type;

    /// @brief Feeds the element types to another template: `into<std::tuple>` → `std::tuple<Ts...>`.
    template<template<class...> class Target>
    using into = Target<Ts...>;

    /// @brief The same list with @p Us appended (no deduplication — see `tessera::unique_t`).
    template<class... Us>
    using append = type_list<Ts..., Us...>;

    /// @brief Concatenation with another `type_list`.
    template<class Other>
    using concat = typename detail::concat<type_list<Ts...>, Other>::type;

    // Deduplication and flattening are deliberately *not* members: a non-template member alias is
    // instantiated together with its class, so `using unique = ...` would deduplicate every list
    // the moment it is named, whether anyone asked for it or not. They live next door as
    // `tessera::unique_t<L>` and `tessera::flatten_t<Ts...>`, which are instantiated on use.

    /// @brief Elements for which `Predicate<T>::value` is `true`, order preserved.
    template<template<class...> class Predicate>
    using filter = typename detail::filter_impl<type_list<Ts...>, Predicate>::type;

    /// @brief `Function<T>` for every element, deduplicated (a mapping may collapse types).
    template<template<class...> class Function>
    using transform = typename detail::transform_impl<type_list<Ts...>, Function>::type;

    /// @brief `Function<T>` for every element, with list-like results spliced in, deduplicated.
    ///
    /// This is how a system collects the union of its components' dependencies: each component
    /// answers with a list, and the lists are merged into one flat set of distinct types.
    template<template<class...> class Function>
    using flat_map = typename detail::flat_map_impl<type_list<Ts...>, Function>::type;
};

/// @brief Customization point: how a type contributes to a flattened list.
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
    using type = typename detail::concat<typename elements_of<Ts>::type...>::type;
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
using concat_t = typename detail::concat<Ls...>::type;

/// @brief A `type_list` with duplicates removed, using the active deduplication backend.
template<class L>
using unique_t = typename detail::unique_of<L>::type;

/// @brief The flat, distinct union of @p Ts — leaves stay, list-like types are spliced in.
///
/// This is the type-level counterpart of `tessera::of<...>`.
template<class... Ts>
using flatten_t = unique_t<concat_t<elements_of_t<Ts>...>>;

namespace detail {

// -- concat --------------------------------------------------------------------------------------
//
// Concatenation is the busiest metafunction in the library: filter, flat_map and the merge step of
// the deduplication all funnel through it, joining a list per element. Written the textbook way —
// fold two lists at a time — it costs one intermediate specialization per input list, each one
// element longer than the last, which is quadratic in retained types and linear in instantiation
// depth. The compiler pays that in gigabytes on a few hundred components.
//
// So it is not written the textbook way. A consteval loop first computes, for every element of the
// *result*, which input list it comes from and at which position; the result type is then produced
// by a single pack expansion over that plan. One instantiation, constant depth, no accumulator.

template<class L, std::size_t I>
struct list_element;

template<class... Ts, std::size_t I>
struct list_element<type_list<Ts...>, I> {
    using type = pack_element_t<I, Ts...>;
};

/// Whether @p T is a `type_list` — used to turn a misuse of `concat_t` into a readable message
/// instead of a substitution failure deep inside the plan.
template<class T>
inline constexpr bool is_type_list = false;

template<class... Ts>
inline constexpr bool is_type_list<type_list<Ts...>> = true;

template<class... Ls>
struct concat {
    static_assert((is_type_list<Ls> && ...), "tessera::concat_t takes type_lists — wrap a bare type in type_list<T>");

private:
    static constexpr std::size_t total = (std::size_t{0} + ... + Ls::size);

    /// For each output position: the index of the input list, and the index within that list.
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
        -> type_list<typename list_element<pack_element_t<plan.first[Is], Ls...>, plan.second[Is]>::type...>;

public:
    using type = decltype(build(std::make_index_sequence<total>{}));
};

template<>
struct concat<> {
    using type = type_list<>;
};

template<class... Ts>
struct concat<type_list<Ts...>> {
    using type = type_list<Ts...>;
};

// -- unique ------------------------------------------------------------------------------------

template<class... Ts>
struct unique_of<type_list<Ts...>> {
    using type = deduplicated_t<Ts...>;
};

// -- filter / transform / flat_map / flatten ---------------------------------------------------

template<class... Ts, template<class...> class Predicate>
struct filter_impl<type_list<Ts...>, Predicate> {
    using type = typename concat<
        std::conditional_t<static_cast<bool>(Predicate<Ts>::value), type_list<Ts>, type_list<>>...>::type;
};

template<template<class...> class Predicate>
struct filter_impl<type_list<>, Predicate> {
    using type = type_list<>;
};

template<class... Ts, template<class...> class Function>
struct transform_impl<type_list<Ts...>, Function> {
    using type = typename unique_of<type_list<Function<Ts>...>>::type;
};

template<class... Ts, template<class...> class Function>
struct flat_map_impl<type_list<Ts...>, Function> {
    using type = typename unique_of<typename concat<elements_of_t<Function<Ts>>...>::type>::type;
};

template<template<class...> class Function>
struct flat_map_impl<type_list<>, Function> {
    using type = type_list<>;
};

template<class... Ts>
struct flatten_impl<type_list<Ts...>> {
    using type = typename unique_of<typename concat<elements_of_t<Ts>...>::type>::type;
};

// -- nth ---------------------------------------------------------------------------------------

#if TESSERA_HAS_TYPE_PACK_ELEMENT
template<std::size_t I, class... Ts>
struct nth_impl {
    static_assert(I < sizeof...(Ts), "tessera::type_list::nth — index out of range");
    using type = __type_pack_element<I, Ts...>;
};
#else
template<std::size_t I, class Head, class... Tail>
struct nth_impl<I, Head, Tail...> : nth_impl<I - 1, Tail...> {};

template<class Head, class... Tail>
struct nth_impl<0, Head, Tail...> {
    using type = Head;
};
#endif

}  // namespace detail
}  // namespace tessera

#endif  // TESSERA_TYPE_LIST_HPP
