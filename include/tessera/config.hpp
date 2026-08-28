// Copyright 2026 Denis Tishkov
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#ifndef TESSERA_CONFIG_HPP
#define TESSERA_CONFIG_HPP

/// @file config.hpp
/// @brief Compiler feature detection and library-wide configuration knobs.
///
/// Everything vendor specific in Tessera is detected here and nowhere else: the rest of the
/// library only ever reads the `TESSERA_*` macros defined below. Compiler builtins are used as
/// an *optimization*, never as a requirement — every backend has a portable fallback that is
/// selected automatically.

#define TESSERA_VERSION_MAJOR 1
#define TESSERA_VERSION_MINOR 1
#define TESSERA_VERSION_PATCH 0
#define TESSERA_VERSION_STRING "1.1.0"

// ---------------------------------------------------------------------------------------------
// Language baseline
// ---------------------------------------------------------------------------------------------

#if defined(_MSC_VER) && !defined(__clang__)
#  define TESSERA_CPLUSPLUS _MSVC_LANG
#else
#  define TESSERA_CPLUSPLUS __cplusplus
#endif

#if TESSERA_CPLUSPLUS < 202302L
#  error "Tessera requires C++23 or newer (it uses explicit object parameters)."
#endif

// ---------------------------------------------------------------------------------------------
// Vendor feature probes — Clang-only builtins stay behind __clang__
// ---------------------------------------------------------------------------------------------

/// @def TESSERA_HAS_BUILTIN
/// @brief `__has_builtin` shim; always 0 on compilers that do not provide the trait.
#if defined(__has_builtin)
#  define TESSERA_HAS_BUILTIN(x) __has_builtin(x)
#else
#  define TESSERA_HAS_BUILTIN(x) 0
#endif

/// @def TESSERA_HAS_FEATURE
/// @brief `__has_feature` shim; always 0 on compilers that do not provide the trait.
#if defined(__has_feature)
#  define TESSERA_HAS_FEATURE(x) __has_feature(x)
#else
#  define TESSERA_HAS_FEATURE(x) 0
#endif

/// @def TESSERA_HAS_BUILTIN_DEDUP_PACK
/// @brief Clang 22+ `__builtin_dedup_pack<Ts...>` — removes duplicates from a type pack in one
///        step, without the O(N^2) template instantiations a library fold needs.
#if defined(__clang__) && TESSERA_HAS_BUILTIN(__builtin_dedup_pack)
#  define TESSERA_HAS_BUILTIN_DEDUP_PACK 1
#else
#  define TESSERA_HAS_BUILTIN_DEDUP_PACK 0
#endif

/// @def TESSERA_HAS_TYPE_PACK_ELEMENT
/// @brief Clang `__type_pack_element<I, Ts...>` — O(1) pack indexing.
#if defined(__clang__) && TESSERA_HAS_BUILTIN(__type_pack_element)
#  define TESSERA_HAS_TYPE_PACK_ELEMENT 1
#else
#  define TESSERA_HAS_TYPE_PACK_ELEMENT 0
#endif

/// @def TESSERA_HAS_REFLECTION
/// @brief P2996 static reflection (`^^T`, splicers, `std::meta`).
///
/// Three spellings are accepted, because the feature is younger than its feature-test macro: a
/// conforming implementation defines `__cpp_impl_reflection` and ships `<meta>`; some builds define
/// `__cpp_reflection`; and the P2996 reference fork of Clang (`-freflection-latest`) defines
/// neither — it only answers `__has_feature(reflection)` — while shipping both `<meta>` and
/// `<experimental/meta>` in its libc++. Whichever header is found is exposed as
/// TESSERA_REFLECTION_HEADER, so that no other file has to repeat the probe.
#if defined(__cpp_impl_reflection) || defined(__cpp_reflection) || TESSERA_HAS_FEATURE(reflection)
#  if __has_include(<meta>)
#    define TESSERA_HAS_REFLECTION 1
#    define TESSERA_REFLECTION_HEADER <meta>
#  elif __has_include(<experimental/meta>)
#    define TESSERA_HAS_REFLECTION 1
#    define TESSERA_REFLECTION_HEADER <experimental/meta>
#  else
#    define TESSERA_HAS_REFLECTION 0
#  endif
#else
#  define TESSERA_HAS_REFLECTION 0
#endif

// ---------------------------------------------------------------------------------------------
// Algebra implementation selection
// ---------------------------------------------------------------------------------------------

#define TESSERA_ALGEBRA_PORTABLE 0    ///< Templates: hybrid fold + divide-and-conquer. Everywhere.
#define TESSERA_ALGEBRA_BUILTIN 1     ///< Templates plus Clang's `__builtin_dedup_pack`.
#define TESSERA_ALGEBRA_REFLECTION 2  ///< P2996 static reflection (see docs/reflection.md).
#define TESSERA_ALGEBRA_FOLD 3        ///< Templates with the textbook fold: the benchmark reference.

// The macro was called TESSERA_DEDUP_BACKEND while deduplication was the only thing behind the
// seam. Both spellings still work, and the values kept their names too.
#define TESSERA_DEDUP_PORTABLE TESSERA_ALGEBRA_PORTABLE
#define TESSERA_DEDUP_BUILTIN TESSERA_ALGEBRA_BUILTIN
#define TESSERA_DEDUP_REFLECTION TESSERA_ALGEBRA_REFLECTION
#define TESSERA_DEDUP_FOLD TESSERA_ALGEBRA_FOLD

/// @def TESSERA_ALGEBRA_BACKEND
/// @brief Which implementation of the list algebra the library uses.
///
/// Defined automatically unless the build system pins it, e.g.
/// `-DTESSERA_ALGEBRA_BACKEND=TESSERA_ALGEBRA_PORTABLE` to measure the fallback on a compiler that
/// would otherwise take a faster path.
#if defined(TESSERA_DEDUP_BACKEND) && !defined(TESSERA_ALGEBRA_BACKEND)
#  define TESSERA_ALGEBRA_BACKEND TESSERA_DEDUP_BACKEND
#endif

#if !defined(TESSERA_ALGEBRA_BACKEND)
#  if TESSERA_HAS_REFLECTION && defined(TESSERA_ENABLE_REFLECTION_BACKEND)
#    define TESSERA_ALGEBRA_BACKEND TESSERA_ALGEBRA_REFLECTION
#  elif TESSERA_HAS_BUILTIN_DEDUP_PACK
#    define TESSERA_ALGEBRA_BACKEND TESSERA_ALGEBRA_BUILTIN
#  else
#    define TESSERA_ALGEBRA_BACKEND TESSERA_ALGEBRA_PORTABLE
#  endif
#endif

/// @brief Human-readable name of the active backend, handy in diagnostics and benchmarks.
#if TESSERA_ALGEBRA_BACKEND == TESSERA_ALGEBRA_BUILTIN
#  define TESSERA_ALGEBRA_BACKEND_NAME "templates + __builtin_dedup_pack"
#elif TESSERA_ALGEBRA_BACKEND == TESSERA_ALGEBRA_REFLECTION
#  define TESSERA_ALGEBRA_BACKEND_NAME "reflection (P2996)"
#elif TESSERA_ALGEBRA_BACKEND == TESSERA_ALGEBRA_FOLD
#  define TESSERA_ALGEBRA_BACKEND_NAME "templates, linear fold"
#else
#  define TESSERA_ALGEBRA_BACKEND_NAME "templates, hybrid fold + merge"
#endif

#endif  // TESSERA_CONFIG_HPP
