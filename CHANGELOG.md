# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the project follows
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1.0] — 2026-08-28

The seam between the public types and their implementation widened from one operation to five, and
the static-reflection path became a full implementation of the algebra rather than a deduplication
backend. Numbers in `docs/benchmarks.md`.

### Added

* `detail::refl` implements **all five operations** of the algebra with static reflection, not just
  deduplication. Measured against the template implementation on the P2996 fork of Clang, the
  assembly step costs a fifth less time and about a fortieth of the compiler memory (15 MiB against
  569 MiB at 256 components); the totals and the method are in `docs/benchmarks.md` §1b.

### Changed

* **`tessera/dedup.hpp` is now `tessera/algebra.hpp`.** It selects between two implementations of
  the whole list algebra — `detail::tmpl` (templates) and `detail::refl` (static reflection) —
  exposed as `tessera::detail::ops`.
* **`tessera::dedup_backend_name` is now `tessera::algebra_backend_name`**, and
  `TESSERA_DEDUP_BACKEND` is now `TESSERA_ALGEBRA_BACKEND` (the former spelling is still accepted by
  both the header and CMake).
* The operations take the destination template, so `tessera::of<...>`, `mosaic::add`,
  `mosaic::filter`, `mosaic::transform` and `mosaic::flat_map` produce a mosaic in one step instead
  of building a `type_list` and converting it.
* `tests/dedup_backends.test.cpp` became `tests/algebra_backends.test.cpp` and now compares the two
  implementations operation by operation, not just their deduplication.

## [1.0.0] — 2026-08-28

First public release.

### Added

* `tessera::mosaic<Ts...>` — heterogeneous container with one value per type: type-keyed `get`,
  `set`, `emplace`, `for_each`, `for_each_type`, `apply`, defaulted equality, `[[no_unique_address]]`
  storage, and construction by type-matched arguments or a broadcast value.
* `tessera::of<...>` / `mapped_of<...>` — assembly of a mosaic from a redundant, nested list of
  types.
* `tessera::type_list<Ts...>` and its algebra: `contains`, `index_of`, `nth`, `into`, `append`,
  `concat`, `filter`, `transform`, `flat_map`, plus the free `unique_t`, `flatten_t`, `concat_t`
  and the `elements_of` customization point.
* `tessera::value_list<T, Vs...>` — compile-time constants with `for_each`, `visit_until` and
  `dispatch`, the bridge from a runtime value back to a template argument.
* Four interchangeable deduplication backends selected by `TESSERA_ALGEBRA_BACKEND`: a portable
  hybrid, Clang's `__builtin_dedup_pack`, a P2996 reflection implementation, and the reference fold.
  All of them produce identical types, which the test suite asserts for every implementation the
  toolchain can compile.
* The reflection backend is implemented and verified against the P2996 reference fork of Clang
  (`docs/reflection.md`): the suite passes with it selected, and it costs about a third less
  compiler memory than the template path at 128 components.
* `tessera::type_name<T>()` and the static-reflection seam described in `docs/reflection.md`.
* Benchmarks for compile time and compiler memory (`benchmarks/run_compile_bench.py`) and for
  runtime dispatch and object size (`benchmarks/runtime`).
* Dependency-free test suite — including a cross-backend equivalence test that compares every
  deduplication implementation the toolchain supports against the reference fold — three worked
  examples, CMake package with install and `find_package(tessera)` support.
