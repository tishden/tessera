// Copyright 2026 Denis Tishkov
// SPDX-License-Identifier: Apache-2.0

#include <type_traits>
#include <vector>

#include <tessera/value_list.hpp>

#include "check.hpp"

namespace {

using tessera::to_value_list_t;
using tessera::type_list;
using tessera::value_list;

enum class Format { Png = 1, Jpeg = 2, WebP = 7 };

template<Format F>
struct Decoder {
    static constexpr Format format = F;
};

template<Format... Fs>
struct DecoderTable {
    static constexpr int count = sizeof...(Fs);
};

using Formats = value_list<Format, Format::Png, Format::Jpeg, Format::WebP>;

static_assert(Formats::size == 3);
static_assert(!Formats::empty);
static_assert(value_list<int>::empty);
static_assert(std::is_same_v<Formats::value_type, Format>);
static_assert(Formats::contains(Format::WebP));
static_assert(!Formats::contains(static_cast<Format>(99)));

static_assert(std::is_same_v<Formats::into<DecoderTable>, DecoderTable<Format::Png, Format::Jpeg, Format::WebP>>);
static_assert(std::is_same_v<Formats::into_types<type_list, Decoder>,
                             type_list<Decoder<Format::Png>, Decoder<Format::Jpeg>, Decoder<Format::WebP>>>);
static_assert(
    std::is_same_v<Formats::as_type_list,
                   type_list<std::integral_constant<Format, Format::Png>, std::integral_constant<Format, Format::Jpeg>,
                             std::integral_constant<Format, Format::WebP>>>);

static_assert(
    std::is_same_v<to_value_list_t<int, type_list<std::integral_constant<int, 1>, std::integral_constant<int, 4>>>,
                   value_list<int, 1, 4>>);

// `dispatch` is usable in a constant expression, which is the cheapest possible proof that it
// costs nothing beyond the comparison chain.
constexpr int dispatched = [] {
    int result = 0;
    Formats::dispatch(Format::Jpeg, [&]<Format I>() { result = static_cast<int>(I); });
    return result;
}();
static_assert(dispatched == 2);

}  // namespace

TESSERA_TEST(for_each_visits_every_constant) {
    std::vector<int> seen;
    Formats::for_each([&]<Format I>() { seen.push_back(static_cast<int>(I)); });
    CHECK_EQ(seen.size(), 3u);
    CHECK_EQ(seen[0], 1);
    CHECK_EQ(seen[2], 7);
}

TESSERA_TEST(for_each_accepts_an_integral_constant_handler) {
    int sum = 0;
    Formats::for_each([&](auto constant) { sum += static_cast<int>(decltype(constant)::value); });
    CHECK_EQ(sum, 10);
}

TESSERA_TEST(visit_until_stops_at_the_first_true) {
    int visits = 0;
    const bool found = Formats::visit_until([&]<Format I>() {
        ++visits;
        return I == Format::Jpeg;
    });
    CHECK(found);
    CHECK_EQ(visits, 2);
}

TESSERA_TEST(visit_until_reports_no_match) {
    const bool found = Formats::visit_until([]<Format>() { return false; });
    CHECK(!found);
}

TESSERA_TEST(dispatch_turns_a_runtime_value_into_a_template_argument) {
    const Format runtimeValue = Format::WebP;

    int handled = 0;
    const bool matched = Formats::dispatch(runtimeValue, [&]<Format I>() {
        // Inside the handler the format is a compile-time constant again.
        handled = Decoder<I>::format == I ? static_cast<int>(I) : 0;
    });

    CHECK(matched);
    CHECK_EQ(handled, 7);
}

TESSERA_TEST(dispatch_reports_an_unknown_value) {
    int calls = 0;
    const bool matched = Formats::dispatch(static_cast<Format>(42), [&]<Format>() { ++calls; });
    CHECK(!matched);
    CHECK_EQ(calls, 0);
}
