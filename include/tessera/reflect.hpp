// Copyright 2026 Denis Tishkov
// SPDX-License-Identifier: Apache-2.0

#ifndef TESSERA_REFLECT_HPP
#define TESSERA_REFLECT_HPP

#include <string_view>

#include "tessera/config.hpp"
#include "tessera/type_list.hpp"

#if TESSERA_HAS_REFLECTION
#  include TESSERA_REFLECTION_HEADER
#  include <vector>
#endif

/// @file reflect.hpp
/// @brief Type introspection: readable type names today, static reflection tomorrow.
///
/// Tessera does everything it does with the type system alone, which means diagnostics are the
/// one thing it cannot do well: a compiler error prints a mangled 400-character type and a
/// `for_each_type` handler has no name to log. `type_name` fills that gap portably, and the
/// reflection section below is the seam where P2996 replaces the string surgery — and where the
/// library gains the operations that are impossible without it (see docs/reflection.md).

namespace tessera {
namespace detail {

/// Extracts the `T = ...` substring the compiler bakes into its pretty function name.
consteval std::string_view parse_type_name(std::string_view signature) noexcept {
    constexpr std::string_view marker = "T = ";
    const std::size_t start = signature.find(marker);
    if (start == std::string_view::npos) {
        return signature;
    }
    const std::size_t from = start + marker.size();
    std::size_t to = signature.size();
    for (std::size_t i = from; i < signature.size(); ++i) {
        if (signature[i] == ']' || signature[i] == ';') {
            to = i;
            break;
        }
    }
    return signature.substr(from, to - from);
}

}  // namespace detail

/// @brief A human-readable name for @p T, usable in `static_assert` messages, tests and logs.
///
/// @code
/// static_assert(tessera::type_name<double>() == "double");
/// @endcode
template<class T>
consteval std::string_view type_name() noexcept {
#if TESSERA_HAS_REFLECTION
    return std::meta::display_string_of(^^T);
#elif defined(__clang__) || defined(__GNUC__)
    return detail::parse_type_name(__PRETTY_FUNCTION__);
#elif defined(_MSC_VER)
    return detail::parse_type_name(__FUNCSIG__);
#else
    return "<unknown>";
#endif
}

// ---------------------------------------------------------------------------------------------
// Static reflection seam (P2996)
//
// Nothing below is enabled on today's toolchains. It is kept in tree, compiled the moment a
// compiler defines __cpp_impl_reflection, because it is the point of the whole layering: the
// public API (mosaic, type_list) is expressed in terms of a handful of metafunctions, and each
// of them has a reflection implementation that is shorter and cheaper than its template
// counterpart. See docs/reflection.md for the full plan and the acceptance criteria.
// ---------------------------------------------------------------------------------------------

#if TESSERA_HAS_REFLECTION

/// @brief The member types of an aggregate, as a `tessera::type_list`.
///
/// Without reflection a mosaic has to be *told* its element types; with reflection it can read them
/// off an existing struct, which turns a hand-written configuration aggregate into a
/// type-addressable container without restating anything.
///
/// @code
/// struct Config { int limit; double rate; };
/// static_assert(std::same_as<tessera::members_of_t<Config>, tessera::type_list<int, double>>);
/// using Mosaic = tessera::members_of_t<Config>::into<tessera::mosaic>;   // distinct types only
/// @endcode
///
/// @note An aggregate whose members repeat a type cannot become a mosaic — a mosaic holds one value
///       per type. `type_list` keeps the repeats; `tessera::unique_t` collapses them if that is what
///       the caller wants.
template<class Aggregate>
consteval auto member_type_reflections() -> std::vector<std::meta::info> {
    // The access-context parameter arrived in P2996R10 and is what C++26 standardizes; both
    // implementations that exist have it. There used to be an `if constexpr` here selecting a
    // pre-R10 single-argument overload, and it had to go: a discarded `if constexpr` branch is
    // still parsed, an arity mismatch is diagnosable without instantiating anything, and GCC 16
    // rightly reports it as an error rather than a warning. A dead branch that breaks a released
    // compiler is worse than no branch, so the requirement is asserted instead.
    static_assert(requires { typename std::meta::access_context; },
                  "tessera: this reflection implementation predates P2996R10 and is not supported");

    std::vector<std::meta::info> types;
    for (const std::meta::info member :
         std::meta::nonstatic_data_members_of(^^Aggregate, std::meta::access_context::current())) {
        types.push_back(std::meta::type_of(member));
    }
    return types;
}

template<class Aggregate>
consteval std::meta::info members_of_info() {
    return std::meta::substitute(^^type_list, member_type_reflections<Aggregate>());
}

template<class Aggregate>
using members_of_t = [:members_of_info<Aggregate>():];

#endif  // TESSERA_HAS_REFLECTION

}  // namespace tessera

#endif  // TESSERA_REFLECT_HPP
