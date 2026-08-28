// Copyright 2026 Denis Tishkov
// SPDX-License-Identifier: Apache-2.0

#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include <tessera/mosaic.hpp>

#include "check.hpp"

namespace {

using tessera::mosaic;
using tessera::of;
using tessera::type_list;

struct Empty {};
struct AlsoEmpty {};

struct Counter {
    int value = 0;
};

struct NoDefault {
    explicit NoDefault(int v) : value(v) {}
    int value;
};

struct Config {
    int limit = 5;
};

struct FromConfig {
    explicit FromConfig(const Config& config) : limit(config.limit) {}
    int limit;
};

struct AlsoFromConfig {
    explicit AlsoFromConfig(const Config& config) : doubled(config.limit * 2) {}
    int doubled;
};

// -- assembly ------------------------------------------------------------------------------------

static_assert(std::is_same_v<of<int, double, int>, mosaic<int, double>>);
static_assert(std::is_same_v<of<int, of<double, int>, type_list<char>>, mosaic<int, double, char>>);
static_assert(std::is_same_v<of<>, mosaic<>>);
static_assert(std::is_same_v<of<mosaic<>, int>, mosaic<int>>);
static_assert(mosaic<int, double>::size == 2);
static_assert(mosaic<>::empty);
static_assert(mosaic<int, double>::contains<int>);
static_assert(!mosaic<int, double>::contains<char>);
static_assert(std::is_same_v<mosaic<int, char>::types, type_list<int, char>>);
static_assert(std::is_same_v<mosaic<int, char>::into<std::tuple>, std::tuple<int, char>>);

// -- type algebra on a mosaic ----------------------------------------------------------------------

template<class T>
struct is_integral : std::bool_constant<std::is_integral_v<T>> {};

template<class T>
using add_pointer_t = T*;

template<class T>
using dependencies_of = typename T::dependencies;

struct Reader {
    using dependencies = type_list<int, char>;
};

struct Writer {
    using dependencies = type_list<char, double>;
};

static_assert(std::is_same_v<mosaic<int, double>::add<char, int>, mosaic<int, double, char>>);
static_assert(std::is_same_v<mosaic<int, double, char>::filter<is_integral>, mosaic<int, char>>);
static_assert(std::is_same_v<mosaic<int, char>::transform<add_pointer_t>, mosaic<int*, char*>>);
static_assert(std::is_same_v<of<Reader, Writer>::flat_map<dependencies_of>, mosaic<int, char, double>>);

// -- constant extraction ---------------------------------------------------------------------------

enum class Backend { Cpu, Gpu };

template<class T>
struct backend_of {
    static constexpr Backend value = T::backend;
};

struct CpuMeshPass {
    static constexpr Backend backend = Backend::Cpu;
};

struct CpuTextPass {
    static constexpr Backend backend = Backend::Cpu;
};

struct GpuMeshPass {
    static constexpr Backend backend = Backend::Gpu;
};

using Backends = of<CpuMeshPass, CpuTextPass, GpuMeshPass>::values<Backend, backend_of>;
static_assert(Backends::size == 2);
static_assert(Backends::contains(Backend::Cpu));
static_assert(Backends::contains(Backend::Gpu));

// -- storage -----------------------------------------------------------------------------------------

static_assert(sizeof(mosaic<Empty, AlsoEmpty>) == 1, "stateless elements must not cost anything");
static_assert(std::is_trivially_copyable_v<mosaic<int, double>>);
static_assert(std::is_empty_v<mosaic<>>);

// -- reference semantics of get() ----------------------------------------------------------------------

using Pack = mosaic<int, double>;
static_assert(std::is_same_v<decltype(std::declval<Pack&>().get<int>()), int&>);
static_assert(std::is_same_v<decltype(std::declval<const Pack&>().get<int>()), const int&>);
static_assert(std::is_same_v<decltype(std::declval<Pack&&>().get<int>()), int&&>);
static_assert(std::is_same_v<decltype(std::declval<const Pack&&>().get<int>()), const int&&>);

}  // namespace

TESSERA_TEST(default_construction_value_initializes_every_element) {
    mosaic<int, double, Counter> pack;
    CHECK_EQ(pack.get<int>(), 0);
    CHECK_EQ(pack.get<double>(), 0.0);
    CHECK_EQ(pack.get<Counter>().value, 0);
}

TESSERA_TEST(constructor_matches_arguments_by_type_in_any_order) {
    mosaic<int, double, std::string> pack{std::string{"answer"}, 42, 2.5};
    CHECK_EQ(pack.get<int>(), 42);
    CHECK_EQ(pack.get<double>(), 2.5);
    CHECK_EQ(pack.get<std::string>(), "answer");
}

TESSERA_TEST(constructor_value_initializes_the_elements_left_out) {
    mosaic<int, double, std::string> pack{7};
    CHECK_EQ(pack.get<int>(), 7);
    CHECK_EQ(pack.get<double>(), 0.0);
    CHECK(pack.get<std::string>().empty());
}

TESSERA_TEST(constructor_moves_rather_than_copies) {
    std::string source(64, 'x');
    mosaic<std::string> pack{std::move(source)};
    CHECK_EQ(pack.get<std::string>().size(), 64u);
    CHECK(source.empty());  // NOLINT(bugprone-use-after-move) — checking the move happened
}

TESSERA_TEST(elements_without_a_default_constructor_can_still_be_supplied) {
    mosaic<NoDefault, int> pack{NoDefault{3}, 9};
    CHECK_EQ(pack.get<NoDefault>().value, 3);
    CHECK_EQ(pack.get<int>(), 9);
}

TESSERA_TEST(broadcast_builds_every_element_from_one_value) {
    const Config config{11};
    mosaic<FromConfig, AlsoFromConfig> pack{tessera::broadcast(config)};
    CHECK_EQ(pack.get<FromConfig>().limit, 11);
    CHECK_EQ(pack.get<AlsoFromConfig>().doubled, 22);
}

TESSERA_TEST(set_and_emplace_replace_an_element) {
    mosaic<int, std::string> pack;
    pack.set(5);
    pack.set(std::string{"five"});
    CHECK_EQ(pack.get<int>(), 5);
    CHECK_EQ(pack.get<std::string>(), "five");

    pack.emplace<std::string>(3u, 'a');
    CHECK_EQ(pack.get<std::string>(), "aaa");
}

TESSERA_TEST(get_propagates_constness) {
    mosaic<int> pack{4};
    const mosaic<int>& constPack = pack;
    pack.get<int>() = 6;
    CHECK_EQ(constPack.get<int>(), 6);
}

TESSERA_TEST(for_each_visits_values_in_declaration_order) {
    mosaic<int, double, std::string> pack{1, 2.0, std::string{"three"}};
    std::vector<std::string> seen;
    pack.for_each([&](const auto& value) {
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, std::string>) {
            seen.push_back(value);
        } else {
            seen.push_back(std::to_string(value));
        }
    });
    CHECK_EQ(seen.size(), 3u);
    CHECK_EQ(seen[0], "1");
    CHECK_EQ(seen[2], "three");
}

TESSERA_TEST(for_each_can_mutate_the_elements) {
    mosaic<int, double> pack{1, 1.0};
    pack.for_each([](auto& value) { value += 1; });
    CHECK_EQ(pack.get<int>(), 2);
    CHECK_EQ(pack.get<double>(), 2.0);
}

TESSERA_TEST(for_each_type_accepts_both_handler_spellings) {
    int templateStyle = 0;
    mosaic<int, double, char>::for_each_type([&]<class T>() { templateStyle += static_cast<int>(sizeof(T)); });
    CHECK_EQ(templateStyle, static_cast<int>(sizeof(int) + sizeof(double) + sizeof(char)));

    int identityStyle = 0;
    mosaic<int, double, char>::for_each_type(
        [&](auto tag) { identityStyle += static_cast<int>(sizeof(typename decltype(tag)::type)); });
    CHECK_EQ(identityStyle, templateStyle);
}

TESSERA_TEST(apply_passes_every_element_at_once) {
    mosaic<int, double> pack{2, 0.5};
    const double product = pack.apply([](int lhs, double rhs) { return lhs * rhs; });
    CHECK_EQ(product, 1.0);
}

TESSERA_TEST(equality_compares_element_by_element) {
    const mosaic<int, std::string> lhs{1, std::string{"a"}};
    const mosaic<int, std::string> same{1, std::string{"a"}};
    const mosaic<int, std::string> other{2, std::string{"a"}};
    CHECK(lhs == same);
    CHECK(lhs != other);
}

TESSERA_TEST(copies_and_moves_are_independent) {
    mosaic<int, std::string> original{1, std::string{"one"}};
    mosaic<int, std::string> copy = original;
    copy.set(2);
    CHECK_EQ(original.get<int>(), 1);
    CHECK_EQ(copy.get<int>(), 2);

    const mosaic<int, std::string> moved = std::move(copy);
    CHECK_EQ(moved.get<std::string>(), "one");
}
