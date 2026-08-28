// Copyright 2026 Denis Tishkov
// SPDX-License-Identifier: Apache-2.0

#include <type_traits>

#include <tessera/dedup.hpp>
#include <tessera/type_list.hpp>

#include "check.hpp"

/// @file dedup_backends.test.cpp
/// @brief The backends must produce *identical types*, not merely equivalent behaviour.
///
/// Every implementation available on this toolchain is instantiated on the same input and compared
/// against the reference fold, which is the definition of the expected result: the first occurrence
/// of each type survives, in its original position. A backend that reordered survivors, or kept a
/// later occurrence, would change the layout of every mosaic built through it — silently.

namespace {

using tessera::type_list;

template<int I>
struct Tag {};

// 48 mentions of 24 distinct types, duplicates scattered so that no backend can be right by
// accident: adjacent repeats, distant repeats, and a repeat of the very first element at the end.
template<class Sequence>
struct repeated;

template<std::size_t... Is>
struct repeated<std::index_sequence<Is...>> {
    using type = type_list<Tag<static_cast<int>(Is % 24)>..., Tag<0>>;
};

using Input = repeated<std::make_index_sequence<48>>::type;

template<class L>
struct apply_fold;
template<class... Ts>
struct apply_fold<type_list<Ts...>> {
    using type = tessera::detail::unique_fold_t<Ts...>;
};

template<class L>
struct apply_portable;
template<class... Ts>
struct apply_portable<type_list<Ts...>> {
    using type = tessera::detail::unique_portable_t<Ts...>;
};

using Reference = apply_fold<Input>::type;

static_assert(Reference::size == 24, "24 distinct types go in, 24 come out");
static_assert(std::is_same_v<Reference::nth<0>, Tag<0>>, "first occurrence wins");
static_assert(std::is_same_v<Reference::nth<23>, Tag<23>>, "order is preserved");

// -- portable (hybrid fold + merge) ----------------------------------------------------------------

static_assert(std::is_same_v<apply_portable<Input>::type, Reference>);

// A list longer than the fold threshold, so that the divide-and-conquer half of the hybrid runs —
// and, incidentally, a length at which the plain fold no longer compiles at all.
template<class Sequence>
struct wide;

template<std::size_t... Is>
struct wide<std::index_sequence<Is...>> {
    using type = type_list<Tag<static_cast<int>(Is % 400)>...>;
};

using WideInput = wide<std::make_index_sequence<1200>>::type;
static_assert(apply_portable<WideInput>::type::size == 400);
static_assert(std::is_same_v<apply_portable<WideInput>::type::nth<399>, Tag<399>>);

// -- builtin ---------------------------------------------------------------------------------------

#if TESSERA_HAS_BUILTIN_DEDUP_PACK
template<class L>
struct apply_builtin;
template<class... Ts>
struct apply_builtin<type_list<Ts...>> {
    using type = tessera::detail::unique_builtin_t<Ts...>;
};

static_assert(std::is_same_v<apply_builtin<Input>::type, Reference>);
static_assert(std::is_same_v<apply_builtin<WideInput>::type, apply_portable<WideInput>::type>);
#endif

// -- reflection ------------------------------------------------------------------------------------

#if TESSERA_HAS_REFLECTION
template<class L>
struct apply_reflection;
template<class... Ts>
struct apply_reflection<type_list<Ts...>> {
    using type = tessera::detail::unique_reflection_t<Ts...>;
};

static_assert(std::is_same_v<apply_reflection<Input>::type, Reference>);
static_assert(std::is_same_v<apply_reflection<WideInput>::type, apply_portable<WideInput>::type>);
#endif

}  // namespace

TESSERA_TEST(every_available_deduplication_backend_agrees) {
    CHECK(Reference::size == 24);
    CHECK(apply_portable<WideInput>::type::size == 400);
    std::printf("        backends compared on this build: fold, portable%s%s\n",
                TESSERA_HAS_BUILTIN_DEDUP_PACK ? ", builtin" : "", TESSERA_HAS_REFLECTION ? ", reflection" : "");
}
