# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the project follows
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added — GCC 16

* **The reflection backend builds on a released compiler.** GCC 16.1 (April 2026) implements P2996R13
  behind `-freflection`; the whole suite passes with `TESSERA_ALGEBRA_BACKEND=REFLECTION` on GCC
  16.2, and `graph.test.cpp` and `algebra_backends.test.cpp` pin exact types, so passing under both
  selections is the assertion that the two implementations agree there too.
* `cmake/toolchains/gcc-16-reflection.cmake` — CMake releases predating GCC 16 do not know it can do
  C++26 and silently settle on an older `-std=`, which switches reflection off and produces errors
  from inside `<meta>`. Same trap, same fix, as the clang-p2996 toolchain file.
* `.github/workflows/reflection.yml` now has two jobs: GCC 16 runs on every push and pull request,
  because a released compiler regressing is a library bug; the Bloomberg fork stays weekly and
  non-blocking, because it tracks a moving proposal. Both toolchains are cached.
* [docs/benchmarks.md §1e](docs/benchmarks.md) — the §1c experiment repeated on GCC 16, where it does
  **not** reproduce. Reflection there costs 119 → 470 → 1898 MiB across the same range, growing ×4
  per doubling exactly like the template path, with an advantage that shrinks from 1.90× to 1.46× as
  N grows. "Deduplication through reflection costs no compiler memory" is a property of Clang's
  constant evaluator, not of reflection; what holds on both compilers is that the algorithm stays
  quadratic and reflection is a cheaper representation rather than a better algorithm.
* The mechanism, measured rather than guessed: **GCC's constant evaluator allocates in proportion to
  how much it evaluates**, for any `constexpr` code. A bare serial loop with no types and no
  allocation costs GCC 265 MiB at a million iterations and 3.8 GiB at sixteen million; Clang stays
  flat at 79 MiB however long it runs. Deduplication performs a quadratic number of evaluation steps,
  so on GCC the memory follows the steps — which is a different mechanism from the template path's
  retained class specializations, producing the same curve. On GCC, moving work into constant
  evaluation changes which pool the memory comes from rather than whether it is spent.

### Fixed

* **`reflect.hpp` did not compile on GCC 16.** `member_type_reflections` selected between the
  P2996R10 access-context overload and a pre-R10 one with `if constexpr`. A discarded `if constexpr`
  branch is still parsed, an arity mismatch needs no instantiation to diagnose, and GCC 16's
  `-Wtemplate-body` makes it an error rather than the warning a Clang-only pragma used to suppress.
  Both existing implementations have `access_context`, so the dead branch is gone and the
  requirement is a `static_assert` instead.
* Two lambdas in `algebra_reflection.hpp` are now `[[maybe_unused]]`: with an empty pack (`of<>`,
  `concat_t<>`) the fold never calls them, which GCC reports as set-but-unused. The build is warning
  free on Clang 21, GCC 16 and the fork.
* The compile-time benchmark step in the reflection workflow passed `--extra "-O0 …"`, which argparse
  reads as a missing argument because the value starts with a dash. It never ran; it is `--extra=…`
  now.

### Added — dependency resolution

* **`tessera::resolve<Roots...>` / `resolved_t<Roots...>`** (`tessera/graph.hpp`): the transitive
  closure of a dependency graph, **topologically sorted** so that every element is preceded by
  everything it depends on. `of<...>` flattens a list that is already complete; `resolve` works the
  list out from what each type declares, which is what dependency injection actually asks for.
  Walking the resulting mosaic front to back is a valid start-up order, backwards a valid shutdown
  order, and neither exists as data in the binary.
* **`tessera::dependencies_of<T>`** — the customization point. Defaults to `T::dependencies` when
  that member exists and to `type_list<>` otherwise, so leaves need no opt-in and a third-party type
  is taught by specializing it. A dependency entry may itself be a list, spliced by `elements_of`.
* **`tessera::has_dependency_cycle<Roots...>`** — a cycle makes `resolve` a compile error; this
  reports it as a `bool` instead, so a test can check that a cyclic graph is rejected without
  failing to compile. A node reached by several paths is not a cycle; only a back edge to a node
  still on the current path is.
* **`tessera::is_topologically_sorted<L>`** — the ordering property, stated directly, over a
  `type_list` or a `mosaic`. It also verifies closure: a dependency that is absent answers `npos`,
  which is not less than anything.
* `examples/04_dependency_resolution.cpp` and `tests/graph.test.cpp`, the latter pinning exact
  resolved types (so the suite passing under both algebra implementations is itself the proof that
  they agree here) and asserting the ordering property on a generated 64-node DAG.
* [docs/benchmarks.md §1d](docs/benchmarks.md): resolution costs **1.10 s and 142 MiB at 256
  services against 2.95 s and 558 MiB** for the deduplicating assembly of the same size — following
  the graph is cheaper than flattening a flat list, because a membership test over an existing list
  creates no types while deduplication rebuilds the list. `graph.hpp` costs nothing to a translation
  unit that never names `resolve`, measured with and without it.

### Added — benchmarking

* **A scaling benchmark for the algebra itself.** `bench_tu.cpp` gained two implementations:
  `algebra` (deduplication with no container built on top) and `setup` (the same input with no
  deduplication at all), so that `algebra` minus `setup` is the cost of deduplication with the cost
  of instantiating the types subtracted out. This is what makes the two algebra implementations
  comparable past the point where a container of N elements stops compiling.
* `run_compile_bench.py` gained `--only`, `--timeout`, `--memory-cap` (`RLIMIT_AS` in the child),
  `--stack` (`RLIMIT_STACK`; needed past ~12 000 type mentions, where the compiler segfaults parsing
  the fold) and `--include` (for A/B-ing two implementations of a header). Failures are now
  classified — `depth`, `nesting`, `steps`, `oom`, `crash`, `truncated`, `timeout` — and the first
  `error:` line is reported, because *how* a configuration fails to compile is the result.
* [docs/benchmarks.md §1c](docs/benchmarks.md) — deduplication measured over a 32× range on the
  P2996 fork. Deduplicating with reflection costs the compiler no measurable memory (±21 MiB against
  a translation unit that does not deduplicate at all) while the template implementation grows ×4
  per doubling and passes 21 GiB at 8 000 mentions; both are quadratic in time. Also the four walls
  that stop the measurement, three of which are compiler defaults, and what changes if the linear
  membership scan is replaced with a hash table.
* Documented a hard Clang ceiling: past 65 535 type mentions `sizeof...` silently returns a wrong
  number ([LLVM #119600](https://github.com/llvm/llvm-project/issues/119600)); GCC computes it
  correctly. The `static_assert` on list length in the benchmark is what catches it.
* A `resolve` benchmark implementation (N services in a DAG) alongside `algebra` and `setup`, plus
  `resolve_chain` — the same resolution over a chain, where depth equals size, which is what finds
  the instantiation-depth limit rather than the memory limit.
* [docs/benchmarks.md §1d](docs/benchmarks.md) records how far resolution goes and what stops it:
  510 links in a chain, ~1010 direct dependencies of one service, 512 roots over a shallow DAG —
  all of them the instantiation-depth limit, because the traversal spends a level per element of
  every list on the stack. The limit is the *shape* of the graph, not its size.
* README gained a table of the compiler limits met while measuring — the silent `sizeof...`
  overflow, instantiation depth, the compiler's own stack, the expression nesting limit, the
  constant-evaluation budget, and CMake choosing the wrong standard for the P2996 fork.

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
