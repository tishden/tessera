// Copyright 2026 Denis Tishkov
// SPDX-License-Identifier: Apache-2.0

#include <type_traits>
#include <utility>

#include <tessera/algebra.hpp>
#include <tessera/type_list.hpp>

#include "check.hpp"

/// @file algebra_backends.test.cpp
/// @brief The implementations of the algebra must produce *identical types*, not merely equivalent
///        behaviour.
///
/// Every implementation the toolchain can compile is instantiated on the same input and compared
/// against the template one, operation by operation. An implementation that reordered survivors, or
/// kept a later occurrence, would change the layout of every mosaic built through it — silently.

namespace {

using tessera::type_list;
namespace tmpl = tessera::detail::tmpl;

template<int I>
struct Tag {};

// 48 mentions of 24 distinct types, duplicates scattered so that no implementation can be right by
// accident: adjacent repeats, distant repeats, and a repeat of the very first element at the end.
template<class Sequence>
struct repeated;

template<std::size_t... Is>
struct repeated<std::index_sequence<Is...>> {
    using type = type_list<Tag<static_cast<int>(Is % 24)>..., Tag<0>>;
};

using Input = repeated<std::make_index_sequence<48>>::type;

// A list longer than the fold threshold, so that the divide-and-conquer half of the hybrid runs —
// and, incidentally, a length at which the plain fold no longer compiles at all.
template<class Sequence>
struct wide;

template<std::size_t... Is>
struct wide<std::index_sequence<Is...>> {
    using type = type_list<Tag<static_cast<int>(Is % 400)>...>;
};

using WideInput = wide<std::make_index_sequence<1200>>::type;

// Each variant lives in its own metafunction: a non-template member alias would be instantiated
// with its class, and the fold applied to WideInput does not compile at all (that is the point).
template<class L>
struct by_fold;
template<class... Ts>
struct by_fold<type_list<Ts...>> {
    using type = tmpl::unique_fold_t<Ts...>;
};

template<class L>
struct by_portable;
template<class... Ts>
struct by_portable<type_list<Ts...>> {
    using type = tmpl::unique_portable_t<Ts...>;
};

using Reference = by_fold<Input>::type;

static_assert(Reference::size == 24, "24 distinct types go in, 24 come out");
static_assert(std::is_same_v<Reference::nth<0>, Tag<0>>, "first occurrence wins");
static_assert(std::is_same_v<Reference::nth<23>, Tag<23>>, "order is preserved");
static_assert(std::is_same_v<by_portable<Input>::type, Reference>);
static_assert(by_portable<WideInput>::type::size == 400);
static_assert(std::is_same_v<by_portable<WideInput>::type::nth<399>, Tag<399>>);

#if TESSERA_HAS_BUILTIN_DEDUP_PACK
template<class L>
struct by_builtin;
template<class... Ts>
struct by_builtin<type_list<Ts...>> {
    using type = tmpl::unique_builtin_t<Ts...>;
};

static_assert(std::is_same_v<by_builtin<Input>::type, Reference>);
static_assert(std::is_same_v<by_builtin<WideInput>::type, by_portable<WideInput>::type>);
#endif

// -- the five operations: templates against reflection -----------------------------------------------

#if TESSERA_HAS_REFLECTION
namespace refl = tessera::detail::refl;

template<class L>
struct compare_unique;
template<class... Ts>
struct compare_unique<type_list<Ts...>> {
    static_assert(std::is_same_v<tmpl::unique_into<type_list, Ts...>, refl::unique_into<type_list, Ts...>>);
    static_assert(std::is_same_v<tmpl::nth<0, Ts...>, refl::nth<0, Ts...>>);
    static_assert(std::is_same_v<tmpl::nth<sizeof...(Ts) - 1, Ts...>, refl::nth<sizeof...(Ts) - 1, Ts...>>);
    using ok = void;
};

using check_unique = compare_unique<Input>::ok;
using check_unique_wide = compare_unique<WideInput>::ok;

// concat and splice, on lists that overlap
using Left = type_list<Tag<1>, Tag<2>, Tag<3>>;
using Right = type_list<Tag<3>, Tag<4>>;

static_assert(std::is_same_v<tmpl::concat_into<type_list, Left, Right>, refl::concat_into<type_list, Left, Right>>);
static_assert(
    std::is_same_v<tmpl::concat_into<type_list, Left, Right>, type_list<Tag<1>, Tag<2>, Tag<3>, Tag<3>, Tag<4>>>);
static_assert(
    std::is_same_v<tmpl::splice_unique_into<type_list, Left, Right>, refl::splice_unique_into<type_list, Left, Right>>);
static_assert(
    std::is_same_v<tmpl::splice_unique_into<type_list, Left, Right>, type_list<Tag<1>, Tag<2>, Tag<3>, Tag<4>>>);

// select, with an explicit mask
using Mask = std::integer_sequence<bool, true, false, true, false>;
static_assert(std::is_same_v<tmpl::select_into<type_list, Mask, int, double, char, float>,
                             refl::select_into<type_list, Mask, int, double, char, float>>);
static_assert(std::is_same_v<tmpl::select_into<type_list, Mask, int, double, char, float>, type_list<int, char>>);

// the empty cases, where an implementation is most likely to disagree
static_assert(std::is_same_v<tmpl::unique_into<type_list>, refl::unique_into<type_list>>);
static_assert(std::is_same_v<tmpl::concat_into<type_list>, refl::concat_into<type_list>>);
static_assert(
    std::is_same_v<tmpl::splice_unique_into<type_list, type_list<>>, refl::splice_unique_into<type_list, type_list<>>>);
#endif

}  // namespace

TESSERA_TEST(every_available_implementation_of_the_algebra_agrees) {
    CHECK(Reference::size == 24);
    CHECK(by_portable<WideInput>::type::size == 400);
    std::printf("        compared on this build: fold, portable%s%s\n",
                TESSERA_HAS_BUILTIN_DEDUP_PACK ? ", builtin" : "", TESSERA_HAS_REFLECTION ? ", reflection" : "");
}
