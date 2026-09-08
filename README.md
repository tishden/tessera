# Tessera

**A compile-time, type-keyed container and the type-list algebra that builds it.** Header-only,
C++23, no dependencies.

A *tessera* is a single tile of a mosaic. A `tessera::mosaic<Ts...>` holds exactly one value per
type and is addressed by type rather than by index, and `tessera::of<...>` assembles one from a
list of types that may be redundant, nested, or contributed by parts of the program that know
nothing about each other.

```cpp
#include <tessera/tessera.hpp>

struct Clock {};
struct FrameBuffer { int width, height; };
struct AssetCache  { int loadedMeshes; };

// Every system declares what it needs — nobody maintains a central list.
struct Renderer { using dependencies = tessera::type_list<Clock, FrameBuffer, AssetCache>; };
struct Physics  { using dependencies = tessera::type_list<Clock, FrameBuffer>; };

using Systems = tessera::of<Renderer, Physics>;
using Engine  = Systems::flat_map<tessera::dependencies_of_t>;

// The union of the declarations, deduplicated, resolved before the program runs.
static_assert(std::same_as<Engine, tessera::mosaic<Clock, FrameBuffer, AssetCache>>);

Engine engine;
engine.get<FrameBuffer>().width = 1920;          // addressed by type
engine.for_each([](auto& service) { /* … */ });  // straight-line code, no indirection
```

## Getting it

Header-only and dependency-free: copying `include/tessera` into a project is a valid install.

```cmake
find_package(tessera REQUIRED)          # or add_subdirectory(external/tessera)
target_link_libraries(my_app PRIVATE tessera::tessera)
```

**C++23** — the library uses explicit object parameters. Clang 18+, GCC 14+, MSVC 19.40+. Tested on
Clang 21, Clang 22 (portable, fold and builtin backends), GCC 15, GCC 16 and the P2996 fork of Clang
(all four backends); CI covers Clang 18 and GCC 14 on every backend they can build.

The **reflection backend needs a compiler with P2996**. That is no longer only the experimental fork:
**GCC 16** implements it behind `-freflection`, and
`cmake/toolchains/gcc-16-reflection.cmake` sets the flags CMake does not yet know to set.

| | |
|---|---|
| **What it is** | [Why](#why) · [The three pieces](#the-three-pieces) |
| **What it does** | [Dependency resolution](#dependencies-resolved-and-ordered) · [Runtime → compile-time](#from-a-runtime-value-back-to-a-compile-time-one) · [Two implementations](#two-implementations-of-the-same-algebra) |
| **What it costs** | [Benchmarks](#what-it-costs) · [When not to use it](#when-not-to-use-it) · [Compiler limits](#compiler-limits) |
| **Reference** | [API](docs/reference.md) · [Design](docs/design.md) · [Benchmarks](docs/benchmarks.md) · [Reflection](docs/reflection.md) · [Examples](examples/) |

## Why

Wiring a system out of components usually costs one of three things: a container of base-class
pointers (an indirect call per component and a heap allocation per instance), a hand-maintained
`std::tuple` with index constants that drift out of sync, or a code generator. Tessera does the
wiring in the type system: the set of components is a *type*, it is computed from what the
components themselves declare, and the resulting container is a flat struct with the same layout
you would have written by hand.

* **Addressed by type.** `get<FrameBuffer>()` never shifts when a component is added elsewhere.
* **Assembled, not declared.** `flat_map` collects dependencies, duplicates collapse, empty lists
  disappear. Adding a system adds its services; removing the last user of a service removes it from
  the binary.
* **Resolved transitively, and in order.** `resolve` follows each component's declared dependencies
  to their closure and sorts it topologically, so every element is preceded by everything it needs.
  Walking the container front to back is a valid start-up order and backwards a valid shutdown
  order — neither sequence written by anyone, neither present in the binary as data.
* **Free at run time.** No virtual calls, no type erasure, no allocation; stateless components take
  zero bytes (`[[no_unique_address]]`).
* **Honest about compile time.** The one real cost of this design is what it does to the compiler,
  so the repository measures it — [docs/benchmarks.md](docs/benchmarks.md).

## The three pieces

| Type | Role |
|---|---|
| `tessera::type_list<Ts...>` | a list of types and the algebra over it: `filter`, `transform`, `flat_map`, `concat`, `unique_t`, `flatten_t`, `nth`, `index_of` |
| `tessera::mosaic<Ts...>` | the storage: one value per type, `get<T>()`, `set`, `emplace`, `for_each`, `for_each_type`, `apply`, plus the same algebra lifted to mosaics |
| `tessera::value_list<T, Vs...>` | a list of constants, and `dispatch(runtimeValue, handler)` — the way back from a runtime value to a compile-time one |
| `tessera::resolve<Roots...>` | the transitive closure of a dependency graph, topologically sorted (`tessera/graph.hpp`) |

Configuration and introspection:

| Name | Meaning |
|---|---|
| `tessera/algebra.hpp` | selects the implementation; exposes it as `tessera::detail::ops` |
| `tessera::algebra_backend_name` | which implementation this translation unit was built with |
| `TESSERA_ALGEBRA_BACKEND` | pin it: `TESSERA_ALGEBRA_PORTABLE`, `_BUILTIN`, `_REFLECTION`, `_FOLD` (CMake: `-DTESSERA_ALGEBRA_BACKEND=PORTABLE`) |
| `TESSERA_HAS_REFLECTION`, `TESSERA_REFLECTION_HEADER` | whether the compiler has P2996, and which header it ships |

Full API: [docs/reference.md](docs/reference.md). Design and internals:
[docs/design.md](docs/design.md).

## Dependencies, resolved and ordered

`flat_map` above collects one level: each system's declared dependencies, merged and deduplicated.
That is the right amount when the things being collected are leaves. When they have dependencies of
their own, `resolve<...>` works the whole list out instead — each service declares only its
**direct** dependencies, and the closure, plus the order to start it in, is computed while the
program is being compiled.

```cpp
struct Config         {};
struct Log            {};
struct ConnectionPool { using dependencies = tessera::type_list<Config, Log>; };
struct UserRepository { using dependencies = tessera::type_list<ConnectionPool>; };
struct HttpRouter     { using dependencies = tessera::type_list<UserRepository, Log>; };

using Services = tessera::resolve<HttpRouter>;   // one root; the rest arrives on its own

static_assert(std::same_as<Services,
    tessera::mosaic<Config, Log, ConnectionPool, UserRepository, HttpRouter>>);

Services services;
services.for_each([](auto& service) { service.start(); });
```

Two properties, both `static_assert`-able and both checked in `tests/graph.test.cpp`:

* **closed** — everything reachable from the roots is there, exactly once however many paths lead
  to it (`Log` is named twice above and exists once);
* **topologically sorted** — every element is preceded by what it depends on, so walking the
  container front to back is a valid start-up order and backwards is a valid shutdown order.
  `tessera::is_topologically_sorted<Services>` states the property directly.

A cycle is a compile error, and `tessera::has_dependency_cycle<Roots...>` answers the same question
as a `bool` for code that would rather ask. `tessera::dependencies_of` is the customization point for
types you cannot add a member to. The order comes out of the declarations alone — not include order,
not root order beyond where the walk starts — so it is stable enough to assert on.

Following the graph costs **less** than flattening a flat list of the same size: 1.10 s and 142 MiB
of compiler memory at 256 services, against 2.95 s and 558 MiB for the deduplicating assembly
(Clang 21.1.8, net of the mosaic both build). A membership test over a list that already exists
creates no types, while deduplication rebuilds the list and leaves a specialization behind at every
step.

How far it goes: 512 services in a shallow DAG (14.2 s, 1.2 GiB) before the compiler's instantiation
depth stops it, and **the limit is the shape rather than the size** — the traversal spends a level
per element of every list on the stack, so it is 510 services in a chain, ~1010 direct dependencies
of one service, or 512 roots. A hundred services in a shallow graph cost about a second and come
nowhere near it. Full tables in [docs/benchmarks.md §1d](docs/benchmarks.md).

## From a runtime value back to a compile-time one

```cpp
enum class Format { Png, Jpeg, WebP };

using SupportedFormats = LinkedDecoders::values<Format, format_of>;  // collected from the decoders

SupportedFormats::dispatch(file.format, [&]<Format F>() {
    Decoder<F>::decode(file.bytes);   // F is a compile-time constant again
});
```

Measured at **2.00 ns** per dispatch against **5.13 ns** for an `unordered_map` of function
pointers, and system iteration at **0.49 ns** per call against **5.12 ns** through a vector of
virtual interfaces (see [docs/benchmarks.md](docs/benchmarks.md) for the full setup).

## Two implementations of the same algebra

Everything the library does with type lists — assembling, splicing, deduplicating, filtering — goes
through five operations, and there are two implementations of them:

```
type_list / mosaic
        │
        ▼
detail::ops   unique_into · splice_unique_into · concat_into · select_into · nth
        │
        ├── detail::tmpl   template metaprogramming, every C++23 compiler
        └── detail::refl   static reflection (P2996), one consteval pass per operation
```

Choosing between them is a namespace alias. The public types never mention either.

| Implementation | Requirement | What it does |
|---|---|---|
| `PORTABLE` (default) | any C++23 compiler | a fold for short lists, divide and conquer above 256, membership through a base-class table |
| `BUILTIN` | Clang 22+ | the same, with `__builtin_dedup_pack` for the deduplication step |
| `REFLECTION` | P2996 — GCC 16 `-freflection`, or the Bloomberg fork of Clang | one `consteval` pass over `std::meta::info` per operation, one `substitute` at the end |
| `FOLD` | any C++23 compiler | the textbook linear fold, kept as the benchmark reference |

The operations take the *destination template*, so `tessera::of<...>` produces a `mosaic` in one
step rather than a `type_list` that is immediately converted — one class specialization the compiler
never has to create, on either implementation.

Every compiler-specific spelling lives behind a macro in
[`config.hpp`](include/tessera/config.hpp) — Clang builtins are guarded by `__clang__`, and nothing
outside that header mentions a vendor. Pin an implementation with
`-DTESSERA_ALGEBRA_BACKEND=PORTABLE` (CMake) when you want to measure or reproduce.

Every implementation the toolchain can compile *is* compiled, whether or not it is the one in use,
so `tests/algebra_backends.test.cpp` can run both on the same input and assert they produce the
*identical type*. That is the property that makes the substitution safe, and it is checked rather
than assumed.

## What it costs

Everything here is reproducible from the repository; the method, the full tables and the caveats are
in [docs/benchmarks.md](docs/benchmarks.md). The short version:

**At run time** (`-O2`, Intel Core i7-3820 @ 3.60 GHz, Clang 21.1.8, best of seven) — this is what
the compile-time assembly buys:

| operation | Tessera | the usual alternative |
|---|---|---|
| iterate 8 systems, per call | **0.49 ns** `mosaic::for_each` | 5.12 ns virtual through a `vector` |
| runtime value → compile-time constant | **2.00 ns** `value_list::dispatch` | 5.13 ns `unordered_map` of function pointers |
| `sizeof` a mosaic | identical to `std::tuple` of the same elements | — |

**At compile time**, which is where the design is actually paid for:

* **the container is not the cost.** A `mosaic` of N components compiles within a few percent of a
  `std::tuple` of the same components. Assembling it out of what the components declare is the
  expensive part;
* **assembly is superlinear.** 256 components cost 9.0 s and 793 MiB on the portable backend against
  5.7 s and 305 MiB with Clang 22's `__builtin_dedup_pack` — which is the argument for keeping the
  implementation replaceable. Doubling the count roughly triples the time;
* **resolution is cheaper than assembly**, 1.10 s and 142 MiB at 256 services against 2.95 s and
  558 MiB, because a membership test over a list that already exists creates no types while
  deduplication rebuilds the list;
* the practical range is components **in the tens to low hundreds** per translation unit. Past that
  the answer is fewer types per translation unit, not a cleverer metafunction.

**What reflection changes — and on which compiler.** Deduplication measured on its own, over a 32×
range, on the Bloomberg P2996 fork of Clang:

| type mentions | templates | | reflection | |
|---|---|---|---|---|
| | time | compiler memory | time | compiler memory |
|  1 000 |  3.91 s |  494 MiB |    1.56 s | **−1 MiB** |
|  4 000 | 43.69 s | 7857 MiB |   26.95 s | **−2 MiB** |
| 16 000 | — | — |  434.78 s | **−12 MiB** |
| 32 000 | — | — | 1769.78 s | **−21 MiB** |

There, deduplicating with reflection costs the compiler **no measurable memory at all**: the peak of
the translation unit that deduplicates is the peak of the one that does not, and the difference is as
often negative as positive, while the template path grows ×4 per doubling and passes 21 GiB.

**That result is Clang's, not reflection's.** GCC 16 — the first released compiler to implement
P2996 — runs the same experiment like this:

| type mentions | templates | | reflection | | advantage |
|---|---|---|---|---|---|
| | time | compiler memory | time | compiler memory | |
| 1 000 |  2.66 s |  226 MiB |  2.11 s |  119 MiB | 1.90× memory |
| 2 000 | 10.17 s |  798 MiB |  8.72 s |  470 MiB | 1.70× memory |
| 4 000 | 45.29 s | 2773 MiB | 34.42 s | 1898 MiB | 1.46× memory |

Quadratic on both paths, ×4 per doubling, and the advantage *shrinks* as N grows. So on GCC the
reflection backend buys a constant factor; on the fork it removes the cost entirely. The difference
between those two is implementation quality in the constant evaluator, and today the fork is far
ahead of it.

What holds on both compilers is what the numbers were collected for: deduplication stays quadratic in
time everywhere, and reflection is a cheaper *representation* for the same algorithm rather than a
better algorithm. If compiler memory is the thing killing your CI, measure it on **your** compiler
before trusting any of this — [docs/benchmarks.md §1e](docs/benchmarks.md) is that comparison in
full.

## When not to use it

* **The set is genuinely dynamic** — plugins loaded from `.so`, a component list read from a config
  file, anything that does not exist when the program is compiled. A virtual call costs 5 ns and
  that is a fair price for something you actually need.
* **More than a few hundred types per translation unit.** Everything here is superlinear, and no
  implementation of the algebra changes that.
* **A component needs two of the same thing.** A mosaic holds one value per type; two frame buffers
  means two types (`FrameBuffer<Colour>`, `FrameBuffer<Depth>`), which is usually what a typed
  design wanted anyway.
* **The team is not ready to read it.** A compile error in template-heavy code is its own genre, and
  that is a real operating cost to budget alongside build time.

## Compiler limits

None of these is reached by ordinary use, and the first ceiling anyone actually meets is compiler
memory. They are listed because two of them fail in ways that do not point at themselves, and
because each cost an afternoon to identify.

| Limit | What it looks like | What to do |
|---|---|---|
| **`sizeof...` overflows silently** past 65 535 for a type pack (32 768 for a non-type pack), on Clang | **no diagnostic** — just a wrong number: a 100 000-element pack answers 34 464 | `static_assert` the length of every long list. GCC is correct; [LLVM #119600](https://github.com/llvm/llvm-project/issues/119600) |
| **Instantiation depth** (1024 Clang / 900 GCC) — `resolve` spends a level per list element on the stack: 510 links in a chain, ~1010 dependencies of one service, 512 roots | `recursive template instantiation exceeded maximum depth` | keep graphs shallow and root lists short; `-ftemplate-depth` with `ulimit -s unlimited` |
| **The compiler's own stack**, at fold expressions over ~12 000 arguments | `SIGSEGV`, seconds in, **nothing printed** | `ulimit -s unlimited` |
| **Expression nesting**, 2048 | `instantiating fold expression with 4000 arguments exceeded expression nesting limit` | `-fbracket-depth=131072` |
| **Constant-evaluation budget** (reflection backend) | `not a constant expression`, pointing at the splice rather than the loop | `-fconstexpr-steps` |
| **CMake picks the wrong standard** for the P2996 fork | a wall of errors from inside `<meta>`; reflection was silently off | use `cmake/toolchains/clang-p2996.cmake` |

How each was found, and where each backend stops: [docs/benchmarks.md](docs/benchmarks.md).

## Examples and building

```bash
cmake -S . -B build -G Ninja && cmake --build build
ctest --test-dir build --output-on-failure
```

| Example | Shows |
|---|---|
| [`01_basics`](examples/01_basics.cpp) | building a mosaic, addressing it by type, walking it |
| [`02_system_assembly`](examples/02_system_assembly.cpp) | independent systems declaring dependencies, assembled with `flat_map` |
| [`03_runtime_dispatch`](examples/03_runtime_dispatch.cpp) | a runtime value dispatched back to a compile-time constant |
| [`04_dependency_resolution`](examples/04_dependency_resolution.cpp) | `resolve`: transitive closure, topological order, start-up sequence |

```bash
./build/examples/04_dependency_resolution
./build/benchmarks/bench_runtime                 # object size and dispatch measurements
python3 benchmarks/run_compile_bench.py          # what it costs the compiler
```

## Contributing

Contributions are accepted under the [Apache License 2.0](LICENSE) and require a
[Developer Certificate of Origin](DCO) sign-off (`git commit -s`). See
[CONTRIBUTING.md](CONTRIBUTING.md).

## License

Apache License 2.0 — see [LICENSE](LICENSE) and [NOTICE](NOTICE).

Copyright 2026 Denis Tishkov. Written by Denis Tishkov with Claude (Anthropic) as a
pair-programming tool.
