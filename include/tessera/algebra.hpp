// Copyright 2026 Denis Tishkov
// SPDX-License-Identifier: Apache-2.0

#ifndef TESSERA_ALGEBRA_HPP
#define TESSERA_ALGEBRA_HPP

#include "tessera/config.hpp"
#include "tessera/detail/algebra_reflection.hpp"
#include "tessera/detail/algebra_templates.hpp"

/// @file algebra.hpp
/// @brief Selects the implementation of the list algebra and exposes it as `tessera::detail::ops`.
///
/// Everything the public types do with type lists goes through five operations — `unique_into`,
/// `splice_unique_into`, `concat_into`, `select_into` and `nth`. Two implementations provide them:
/// `detail::tmpl` with template metaprogramming, `detail::refl` with static reflection. Choosing
/// between them is one namespace alias, which is the entire point of the layering.

namespace tessera {

namespace detail {

#if TESSERA_ALGEBRA_BACKEND == TESSERA_ALGEBRA_REFLECTION
#  if !TESSERA_HAS_REFLECTION
#    error "TESSERA_ALGEBRA_BACKEND=REFLECTION, but this compiler has no static reflection."
#  endif
namespace ops = refl;
#else
namespace ops = tmpl;
#endif

}  // namespace detail

/// @brief Name of the algebra implementation compiled into this translation unit.
inline constexpr const char* algebra_backend_name = TESSERA_ALGEBRA_BACKEND_NAME;

}  // namespace tessera

#endif  // TESSERA_ALGEBRA_HPP
