# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the project follows
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] — 2026-08-28

First public release. Extracted from the compile-time assembly layer of a production low-latency
system and rewritten as a standalone library.

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
* Four interchangeable deduplication backends (portable hybrid, Clang builtin, P2996 reflection
  sketch, reference fold) selected by `TESSERA_DEDUP_BACKEND`.
* `tessera::type_name<T>()` and the static-reflection seam described in `docs/reflection.md`.
* Benchmarks for compile time and compiler memory (`benchmarks/run_compile_bench.py`) and for
  runtime dispatch and object size (`benchmarks/runtime`).
* Dependency-free test suite — including a cross-backend equivalence test that compares every
  deduplication implementation the toolchain supports against the reference fold — three worked
  examples, CMake package with install and `find_package(tessera)` support.
