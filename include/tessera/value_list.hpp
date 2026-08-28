// Copyright 2026 Denis Tishkov
// SPDX-License-Identifier: Apache-2.0

#ifndef TESSERA_VALUE_LIST_HPP
#define TESSERA_VALUE_LIST_HPP

#include <cstddef>
#include <type_traits>
#include <utility>

#include "tessera/type_list.hpp"

/// @file value_list.hpp
/// @brief `tessera::value_list` — a compile-time list of constants of one type, with the
///        runtime→compile-time dispatch that turns a value back into a template argument.
///
/// The motivating case: a table of message kinds (or op-codes, or device ids) is fixed when the
/// binary is built, but the value arriving at runtime is an ordinary enum. `dispatch` walks the
/// constants and invokes the handler with the *compile-time* one that matches, so the handler body
/// can instantiate templates on it.

namespace tessera {

namespace detail {

/// Invokes @p f with @p V as a compile-time argument. Both spellings are accepted:
/// `f.template operator()<V>()` and `f(std::integral_constant<..., V>{})`.
template<auto V, class F>
constexpr decltype(auto) invoke_with_constant(F& f) {
    if constexpr (requires { f.template operator()<V>(); }) {
        return f.template operator()<V>();
    } else {
        return f(std::integral_constant<std::remove_cvref_t<decltype(V)>, V>{});
    }
}

template<class T, class L>
struct to_value_list;

}  // namespace detail

/// @brief A compile-time list of constants of type @p T.
///
/// @code
/// using Codes = tessera::value_list<int, 1, 4, 9>;
/// static_assert(Codes::contains(4));
/// Codes::dispatch(runtimeCode, []<int C>() { handle<C>(); });
/// @endcode
template<class T, T... Vs>
struct value_list {
    using value_type = T;

    static constexpr std::size_t size = sizeof...(Vs);
    static constexpr bool empty = (sizeof...(Vs) == 0);

    /// @brief Whether @p value is one of the constants.
    [[nodiscard]] static constexpr bool contains(T value) noexcept {
        return ((value == Vs) || ...);
    }

    /// @brief Invokes @p handler once per constant, in order.
    template<class Handler>
    static constexpr void for_each(Handler&& handler) {
        (detail::invoke_with_constant<Vs>(handler), ...);
    }

    /// @brief Invokes @p handler until one call returns `true`.
    /// @return Whether any call returned `true`.
    template<class Handler>
    static constexpr bool visit_until(Handler&& handler) {
        return (static_cast<bool>(detail::invoke_with_constant<Vs>(handler)) || ...);
    }

    /// @brief Runtime → compile-time dispatch: invokes @p handler with the constant equal to
    ///        @p value, at most once.
    /// @return Whether a constant matched.
    template<class Handler>
    static constexpr bool dispatch(T value, Handler&& handler) {
        return (((value == Vs) ? (detail::invoke_with_constant<Vs>(handler), true) : false) || ...);
    }

    /// @brief Feeds the constants to another template: `into<std::index_sequence>`-style.
    template<template<T...> class Target>
    using into = Target<Vs...>;

    /// @brief Boxes every constant into a type and feeds the result to a type template.
    template<template<class...> class Target, template<T> class Box>
    using into_types = Target<Box<Vs>...>;

    /// @brief The constants as `std::integral_constant` types, for reuse of the type algebra.
    using as_type_list = type_list<std::integral_constant<T, Vs>...>;
};

namespace detail {

template<class T, class... Constants>
struct to_value_list<T, type_list<Constants...>> {
    using type = value_list<T, static_cast<T>(Constants::value)...>;
};

}  // namespace detail

/// @brief Builds a `value_list` from any list of `std::integral_constant`-like types.
template<class T, class L>
using to_value_list_t = typename detail::to_value_list<T, L>::type;

}  // namespace tessera

#endif  // TESSERA_VALUE_LIST_HPP
