// Copyright 2026 Denis Tishkov
// SPDX-License-Identifier: Apache-2.0

#include <string_view>
#include <type_traits>

#include <tessera/mosaic.hpp>
#include <tessera/reflect.hpp>

#include "check.hpp"

namespace {

struct FrameBuffer {};

using tessera::type_name;

static_assert(type_name<int>() == "int");
static_assert(type_name<double>() == "double");
static_assert(type_name<FrameBuffer>().find("FrameBuffer") != std::string_view::npos);

}  // namespace

TESSERA_TEST(type_name_is_usable_for_diagnostics) {
    std::string_view lastSeen;
    tessera::mosaic<int, FrameBuffer>::for_each_type([&]<class T>() { lastSeen = type_name<T>(); });
    CHECK(lastSeen.find("FrameBuffer") != std::string_view::npos);
}

#if TESSERA_HAS_REFLECTION
namespace {

struct Config {
    int limit;
    double rate;
    const char* name;
};

static_assert(std::is_same_v<tessera::members_of_t<Config>, tessera::type_list<int, double, const char*>>);

// The whole point: an existing aggregate becomes a type-addressable mosaic without restating it.
using ConfigMosaic = tessera::members_of_t<Config>::into<tessera::mosaic>;
static_assert(ConfigMosaic::size == 3);
static_assert(ConfigMosaic::contains<double>);

}  // namespace
#endif

TESSERA_TEST(the_active_deduplication_backend_is_reported) {
    CHECK(std::string_view{tessera::dedup_backend_name}.size() > 0);
}
