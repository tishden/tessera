// Copyright 2026 Denis Tishkov
// SPDX-License-Identifier: Apache-2.0

#ifndef TESSERA_TESSERA_HPP
#define TESSERA_TESSERA_HPP

/// @file tessera.hpp
/// @brief Umbrella header — includes the whole library.
///
/// | Header                     | Provides                                                      |
/// |----------------------------|---------------------------------------------------------------|
/// | `tessera/type_list.hpp`    | `type_list`, the type algebra, `flatten_t`, `elements_of`      |
/// | `tessera/mosaic.hpp`       | `mosaic`, `of`, `broadcast`                                    |
/// | `tessera/value_list.hpp`   | `value_list`, runtime → compile-time dispatch                  |
/// | `tessera/reflect.hpp`      | `type_name`, the static-reflection seam                        |
/// | `tessera/algebra.hpp`      | the two implementations of the list algebra and their selection |
/// | `tessera/config.hpp`       | feature detection and configuration macros                     |

#include "tessera/algebra.hpp"
#include "tessera/config.hpp"
#include "tessera/mosaic.hpp"
#include "tessera/reflect.hpp"
#include "tessera/type_list.hpp"
#include "tessera/value_list.hpp"

#endif  // TESSERA_TESSERA_HPP
