// Copyright 2026 Denis Tishkov
// SPDX-License-Identifier: Apache-2.0

#include <string>
#include <tuple>
#include <type_traits>

#include <tessera/type_list.hpp>

#include "check.hpp"

namespace {

using tessera::concat_t;
using tessera::flatten_t;
using tessera::npos;
using tessera::type_list;
using tessera::unique_t;

template<class T>
struct is_integral : std::bool_constant<std::is_integral_v<T>> {};

template<class T>
struct add_pointer {
    using type = T*;
};

template<class T>
using add_pointer_t = T*;

template<class T>
using always_int = int;

struct Node {
    using tessera_elements = type_list<int, char>;
};

// -- shape ------------------------------------------------------------------------------------

static_assert(type_list<>::size == 0);
static_assert(type_list<>::empty);
static_assert(type_list<int, double, int>::size == 3);
static_assert(!type_list<int>::empty);

// -- membership -------------------------------------------------------------------------------

static_assert(type_list<int, double>::contains<int>);
static_assert(!type_list<int, double>::contains<char>);
static_assert(!type_list<>::contains<int>);
static_assert(type_list<int, double>::index_of<double> == 1);
static_assert(type_list<int, double, int>::index_of<int> == 0);
static_assert(type_list<int>::index_of<char> == npos);

// -- indexing ---------------------------------------------------------------------------------

static_assert(std::is_same_v<type_list<int, double, char>::nth<0>, int>);
static_assert(std::is_same_v<type_list<int, double, char>::nth<2>, char>);

// -- into / append / concat -------------------------------------------------------------------

static_assert(std::is_same_v<type_list<int, char>::into<std::tuple>, std::tuple<int, char>>);
static_assert(std::is_same_v<type_list<int>::append<char, double>, type_list<int, char, double>>);
static_assert(std::is_same_v<type_list<int>::concat<type_list<char>>, type_list<int, char>>);
static_assert(std::is_same_v<concat_t<>, type_list<>>);
static_assert(
    std::is_same_v<concat_t<type_list<int>, type_list<char>, type_list<double>>, type_list<int, char, double>>);

// -- unique: first occurrence wins, order preserved --------------------------------------------

static_assert(std::is_same_v<unique_t<type_list<int, char, int, double, char>>, type_list<int, char, double>>);
static_assert(std::is_same_v<unique_t<type_list<>>, type_list<>>);
static_assert(std::is_same_v<unique_t<type_list<int, int, int>>, type_list<int>>);
static_assert(std::is_same_v<unique_t<type_list<const int, int>>, type_list<const int, int>>);

// -- filter -------------------------------------------------------------------------------------

static_assert(std::is_same_v<type_list<int, double, char, float>::filter<is_integral>, type_list<int, char>>);
static_assert(std::is_same_v<type_list<double>::filter<is_integral>, type_list<>>);
static_assert(std::is_same_v<type_list<>::filter<is_integral>, type_list<>>);

// -- transform (deduplicating) ------------------------------------------------------------------

static_assert(std::is_same_v<type_list<int, char>::transform<add_pointer_t>, type_list<int*, char*>>);
static_assert(std::is_same_v<type_list<int, char, double>::transform<always_int>, type_list<int>>);

// -- flat_map: results are spliced in and deduplicated -------------------------------------------

template<class T>
using dependencies_of = typename T::dependencies;

struct Renderer {
    using dependencies = type_list<int, char>;
};

struct Physics {
    using dependencies = type_list<char, double>;
};

struct SelfContained {
    using dependencies = type_list<>;
};

static_assert(std::is_same_v<type_list<Renderer, Physics, SelfContained>::flat_map<dependencies_of>,
                             type_list<int, char, double>>);
static_assert(std::is_same_v<type_list<>::flat_map<dependencies_of>, type_list<>>);

// -- flatten / flatten_t -------------------------------------------------------------------------

static_assert(std::is_same_v<flatten_t<int, type_list<char, type_list<double>>, int>, type_list<int, char, double>>);
static_assert(std::is_same_v<flatten_t<int, type_list<>, type_list<char, int>>, type_list<int, char>>);
static_assert(std::is_same_v<flatten_t<>, type_list<>>);

// -- elements_of is a customization point ---------------------------------------------------------

static_assert(std::is_same_v<tessera::elements_of_t<double>, type_list<double>>);
static_assert(std::is_same_v<tessera::elements_of_t<Node>, type_list<int, char>>);
static_assert(std::is_same_v<flatten_t<Node, double>, type_list<int, char, double>>);

// -- a wider list, to exercise whichever deduplication backend is active ---------------------------

template<int N>
struct Tag {};

using Wide = flatten_t<Tag<0>, Tag<1>, Tag<2>, Tag<3>, Tag<4>, Tag<5>, Tag<6>, Tag<7>, Tag<0>, Tag<3>,
                       type_list<Tag<8>, Tag<1>, Tag<9>>>;
static_assert(Wide::size == 10);
static_assert(std::is_same_v<Wide::nth<8>, Tag<8>>);
static_assert(std::is_same_v<Wide::nth<9>, Tag<9>>);

}  // namespace

TESSERA_TEST(type_list_is_a_compile_time_only_facility) {
    // Everything above is a static_assert; this case exists so the file reports as run.
    CHECK(Wide::size == 10);
    CHECK(type_list<int, double>::index_of<double> == 1);
}
