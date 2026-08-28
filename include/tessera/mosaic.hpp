// Copyright 2026 Denis Tishkov
// SPDX-License-Identifier: Apache-2.0

#ifndef TESSERA_MOSAIC_HPP
#define TESSERA_MOSAIC_HPP

#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

#include "tessera/config.hpp"
#include "tessera/type_list.hpp"
#include "tessera/value_list.hpp"

/// @file mosaic.hpp
/// @brief `tessera::mosaic` — a heterogeneous container holding exactly one value per type, and
///        `tessera::of` — the builder that assembles one from a possibly redundant, possibly
///        nested list of types.
///
/// A mosaic is addressed by type, never by index: `engine.get<FrameBuffer>()`. That is what makes
/// it composable — independently written components can each declare the types they need, the
/// lists get merged and deduplicated at compile time, and every component still finds its
/// dependency at the same place.

namespace tessera {

namespace detail {

/// Number of occurrences of @p T in @p Ts. One variable-template instantiation per element,
/// as opposed to the quadratic chain a list-level uniqueness check would build.
template<class T, class... Ts>
inline constexpr std::size_t occurrences = (std::size_t{std::is_same_v<T, Ts>} + ... + std::size_t{0});

template<class... Ts>
inline constexpr bool all_distinct = ((occurrences<Ts, Ts...> == 1) && ... && true);

/// @brief Storage for one element. `[[no_unique_address]]` lets stateless elements — policies, tag
///        types, components that only exist to be named — cost nothing at all.
template<class T>
struct slot {
    [[no_unique_address]] T value;

    constexpr slot()
        requires std::default_initializable<T>
        : value() {}

    template<class U>
        requires(!std::same_as<std::remove_cvref_t<U>, slot>) && std::constructible_from<T, U&&>
    constexpr explicit slot(U&& init) noexcept(std::is_nothrow_constructible_v<T, U&&>)
        : value(std::forward<U>(init)) {}

    constexpr slot(const slot&) = default;
    constexpr slot(slot&&) = default;
    constexpr slot& operator=(const slot&) = default;
    constexpr slot& operator=(slot&&) = default;
    constexpr ~slot() = default;

    friend constexpr bool operator==(const slot&, const slot&) = default;
};

/// Derived-to-base lookup used by `mosaic::get`; deduction picks the single matching slot.
template<class T>
constexpr T& slot_value(slot<T>& s) noexcept {
    return s.value;
}

template<class T>
constexpr const T& slot_value(const slot<T>& s) noexcept {
    return s.value;
}

/// `Member` with the const-qualification and value category of `Self` applied to it.
template<class Self, class Member>
using forwarded_t =
    std::conditional_t<std::is_lvalue_reference_v<Self>,
                       std::conditional_t<std::is_const_v<std::remove_reference_t<Self>>, const Member&, Member&>,
                       std::conditional_t<std::is_const_v<std::remove_reference_t<Self>>, const Member&&, Member&&>>;

/// Whether one of @p Args has the decayed type @p T.
template<class T, class... Args>
inline constexpr bool supplied_by = (std::is_same_v<T, std::remove_cvref_t<Args>> || ...);

/// Returns the argument whose decayed type is @p T, or a value-initialized @p T if there is none.
template<class T>
constexpr T pick_or_default() {
    static_assert(std::default_initializable<T>,
                  "tessera::mosaic — an element type is neither passed to the constructor nor default-initializable");
    return T();
}

template<class T, class Head, class... Tail>
constexpr decltype(auto) pick_or_default(Head&& head, Tail&&... tail) {
    if constexpr (std::is_same_v<T, std::remove_cvref_t<Head>>) {
        return std::forward<Head>(head);
    } else {
        return pick_or_default<T>(std::forward<Tail>(tail)...);
    }
}

}  // namespace detail

/// @brief Constructor tag: build *every* element of a mosaic from the same value.
///
/// @code
/// Components components{tessera::broadcast(config)};  // every component sees the config
/// @endcode
///
/// @note The tag holds a reference, so pass it straight into a constructor. Storing one
///       (`auto tag = tessera::broadcast(makeConfig());`) leaves a reference to a dead temporary.
template<class T>
struct broadcast_t {
    const T& value;
};

template<class T>
[[nodiscard]] constexpr broadcast_t<T> broadcast(const T& value) noexcept {
    return broadcast_t<T>{value};
}

/// @brief A heterogeneous container with exactly one value per element type.
///
/// Element types must be distinct; use `tessera::of<...>` to build one from a list that may
/// contain duplicates or nested lists. The container is a flat set of `[[no_unique_address]]`
/// members — no indirection, no runtime type information, no allocation.
///
/// @code
/// tessera::mosaic<int, std::string> m{42, std::string{"answer"}};
/// m.get<int>() += 1;
/// m.for_each([](const auto& value) { print(value); });
/// @endcode
template<class... Ts>
class mosaic : private detail::slot<Ts>... {
    static_assert(detail::all_distinct<Ts...>,
                  "tessera::mosaic requires distinct element types — use tessera::of<...> to deduplicate");

public:
    /// Element types, and the hook that lets a mosaic be spliced into `tessera::of<...>`.
    using tessera_elements = type_list<Ts...>;
    using types = type_list<Ts...>;

    static constexpr std::size_t size = sizeof...(Ts);
    static constexpr bool empty = (sizeof...(Ts) == 0);

    /// @brief Whether @p T is one of the element types.
    template<class T>
    static constexpr bool contains = (std::is_same_v<T, Ts> || ...);

    // -- construction --------------------------------------------------------------------------

    constexpr mosaic() = default;

    /// @brief Builds elements from the given values, matched by type, in any order.
    ///        Element types with no matching argument are value-initialized.
    ///
    /// Two arguments of the same type would leave one of them silently unused, so that is rejected
    /// rather than resolved by position; and an element that is neither supplied nor
    /// default-initializable makes the constructor drop out of overload resolution rather than fail
    /// inside it, so that `std::is_constructible_v` tells the truth about this type.
    template<class... Args>
        requires(sizeof...(Args) > 0) && (sizeof...(Args) <= sizeof...(Ts)) &&
                (contains<std::remove_cvref_t<Args>> && ...) && detail::all_distinct<std::remove_cvref_t<Args>...> &&
                ((detail::supplied_by<Ts, Args...> || std::default_initializable<Ts>) && ...)
    constexpr explicit mosaic(Args&&... args)
        : detail::slot<Ts>(detail::pick_or_default<Ts>(std::forward<Args>(args)...))... {}

    /// @brief Builds every element from one shared value (see `tessera::broadcast`).
    template<class U>
        requires(sizeof...(Ts) > 0) && (std::constructible_from<Ts, const U&> && ...)
    constexpr explicit mosaic(broadcast_t<U> shared) : detail::slot<Ts>(shared.value)... {}

    // -- element access ------------------------------------------------------------------------

    /// @brief The element of type @p T, with the const-qualification and value category of `*this`.
    template<class T, class Self>
    [[nodiscard]] constexpr auto&& get(this Self&& self) noexcept {
        static_assert(contains<T>, "tessera::mosaic::get<T> — T is not an element of this mosaic");
        return static_cast<detail::forwarded_t<Self, T>>(detail::slot_value<T>(self));
    }

    /// @brief Assigns to the element whose type is `std::remove_cvref_t<U>`.
    template<class U>
        requires contains<std::remove_cvref_t<U>>
    constexpr void set(U&& value) {
        get<std::remove_cvref_t<U>>() = std::forward<U>(value);
    }

    /// @brief Replaces the element of type @p T with a freshly constructed one.
    ///        The element is assigned, not destroyed and rebuilt, so @p T must be move-assignable.
    template<class T, class... Args>
        requires contains<T> && std::constructible_from<T, Args...>
    constexpr T& emplace(Args&&... args) {
        T& element = get<T>();
        element = T(std::forward<Args>(args)...);
        return element;
    }

    // -- iteration -----------------------------------------------------------------------------

    /// @brief Invokes @p handler once per element value, in declaration order.
    ///        The values are const if and only if `*this` is.
    template<class Self, class Handler>
    constexpr void for_each(this Self&& self, Handler&& handler) {
        (handler(self.template get<Ts>()), ...);
    }

    /// @brief Invokes @p handler once per element *type*, without touching any value.
    ///        Accepts `handler.template operator()<T>()` and `handler(std::type_identity<T>{})`.
    template<class Handler>
    static constexpr void for_each_type(Handler&& handler) {
        (invoke_with_type<Ts>(handler), ...);
    }

    /// @brief Calls @p handler once with all elements as arguments, `std::apply` style.
    template<class Self, class Handler>
    constexpr decltype(auto) apply(this Self&& self, Handler&& handler) {
        return handler(self.template get<Ts>()...);
    }

    friend constexpr bool operator==(const mosaic&, const mosaic&) = default;

    // -- type algebra --------------------------------------------------------------------------

    /// @brief Feeds the element types to another template.
    template<template<class...> class Target>
    using into = Target<Ts...>;

    /// @brief This mosaic plus @p Us, flattened and deduplicated.
    template<class... Us>
    using add = typename flatten_t<type_list<Ts...>, Us...>::template into<::tessera::mosaic>;

    /// @brief The elements satisfying `Predicate<T>::value`.
    template<template<class...> class Predicate>
    using filter = typename types::template filter<Predicate>::template into<::tessera::mosaic>;

    /// @brief `Function<T>` for every element type, deduplicated.
    template<template<class...> class Function>
    using transform = typename types::template transform<Function>::template into<::tessera::mosaic>;

    /// @brief `Function<T>` for every element type, with list-like results spliced in.
    ///
    /// The workhorse of system assembly: `Services = Systems::flat_map<dependencies_of>`.
    template<template<class...> class Function>
    using flat_map = typename types::template flat_map<Function>::template into<::tessera::mosaic>;

    /// @brief Collects a constant from every element into a `tessera::value_list`, deduplicated:
    ///        `Extractor<T>::value` must be a constant convertible to @p E.
    template<class E, template<class...> class Extractor>
    using values =
        to_value_list_t<E, unique_t<type_list<std::integral_constant<E, static_cast<E>(Extractor<Ts>::value)>...>>>;

private:
    template<class T, class Handler>
    static constexpr void invoke_with_type(Handler& handler) {
        if constexpr (requires { handler.template operator()<T>(); }) {
            handler.template operator()<T>();
        } else {
            handler(std::type_identity<T>{});
        }
    }
};

/// @brief Builds a `mosaic` out of @p Ts: nested lists and mosaics are spliced in, duplicates and
///        empty lists are dropped, and the first occurrence of each type keeps its position.
///
/// @code
/// using Services = tessera::of<Clock, FrameBuffer, tessera::of<Clock, AssetCache>>;
/// static_assert(std::same_as<Services, tessera::mosaic<Clock, FrameBuffer, AssetCache>>);
/// @endcode
template<class... Ts>
using of = typename flatten_t<Ts...>::template into<mosaic>;

/// @brief `tessera::of` with a mapping applied to every type first.
template<template<class...> class Function, class... Ts>
using mapped_of = typename flatten_t<Function<Ts>...>::template into<mosaic>;

}  // namespace tessera

#endif  // TESSERA_MOSAIC_HPP
