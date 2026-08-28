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

template<class T> using dependencies_of = typename T::dependencies;

using Systems = tessera::of<Renderer, Physics>;
using Engine  = Systems::flat_map<dependencies_of>;

// The union of the declarations, deduplicated, resolved before the program runs.
static_assert(std::same_as<Engine, tessera::mosaic<Clock, FrameBuffer, AssetCache>>);

Engine engine;
engine.get<FrameBuffer>().width = 1920;          // addressed by type
engine.for_each([](auto& service) { /* … */ });  // straight-line code, no indirection
```

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

Configuration and introspection:

| Name | Meaning |
|---|---|
| `tessera/algebra.hpp` | selects the implementation; exposes it as `tessera::detail::ops` |
| `tessera::algebra_backend_name` | which implementation this translation unit was built with |
| `TESSERA_ALGEBRA_BACKEND` | pin it: `TESSERA_ALGEBRA_PORTABLE`, `_BUILTIN`, `_REFLECTION`, `_FOLD` (CMake: `-DTESSERA_ALGEBRA_BACKEND=PORTABLE`) |
| `TESSERA_HAS_REFLECTION`, `TESSERA_REFLECTION_HEADER` | whether the compiler has P2996, and which header it ships |

Full API: [docs/reference.md](docs/reference.md). Design and internals:
[docs/design.md](docs/design.md).

## From a runtime value back to a compile-time one

```cpp
enum class Format { Png, Jpeg, WebP };

using SupportedFormats = LinkedDecoders::values<Format, format_of>;  // collected from the decoders

SupportedFormats::dispatch(file.format, [&]<Format F>() {
    Decoder<F>::decode(file.bytes);   // F is a compile-time constant again
});
```

Measured at **2.04 ns** per dispatch against **5.51 ns** for an `unordered_map` of function
pointers, and system iteration at **0.50 ns** per call against **5.25 ns** through a vector of
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
| `REFLECTION` | P2996 (`<meta>` or `<experimental/meta>`) | one `consteval` pass over `std::meta::info` per operation, one `substitute` at the end |
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

## Measured

A library that does its work during translation has to be measured during translation.
`benchmarks/run_compile_bench.py` compiles one translation unit per configuration and reads the
compiler's own resource usage with `os.wait4`, so peak memory is an exact high-water mark rather
than a sample. The workload: N systems declaring four overlapping dependencies each — 4N type
mentions flat-mapped and deduplicated down to N, then assembled into a mosaic.

**Compile time and compiler memory** (Clang 22.1.8, `-std=c++23 -O0`):

| components | `std::tuple` of N | `mosaic` of N | assembly, portable | assembly, builtin | assembly, fold |
|---|---|---|---|---|---|
| 64  | 0.80 s / 114 MiB | 0.74 s / 101 MiB | 1.18 s / 136 MiB | 0.95 s / 113 MiB | 1.19 s / 135 MiB |
| 128 | 1.45 s / 162 MiB | 1.50 s / 128 MiB | 2.93 s / 270 MiB | 2.07 s / 157 MiB | 2.87 s / 241 MiB |
| 256 | 3.46 s / 316 MiB | 4.54 s / 227 MiB | 8.98 s / 793 MiB | 5.71 s / 305 MiB | **fails** |

Reading it: a `mosaic` costs about what a `std::tuple` of the same components costs — the container
is not where the time goes. Assembling it out of what the components declare is, and that is the
part the implementations differ in. The fold stops compiling entirely at 256 components: one
instantiation level per element runs past Clang's 1024-deep limit.

**Run time** (`-O2`, Intel Core i7-3820) — what the compile-time assembly buys:

| operation | Tessera | the usual alternative |
|---|---|---|
| iterate 8 systems, per call | **0.50 ns** `mosaic::for_each` | 5.34 ns virtual through a `vector` |
| runtime value → compile-time constant | **2.05 ns** `value_list::dispatch` | 5.34 ns `unordered_map` of function pointers |
| `sizeof` a mosaic | identical to `std::tuple` of the same elements | — |

**What the reflection implementation changes.** On the P2996 fork of Clang, where both
implementations can be built and compared, subtracting the `mosaic` column isolates the assembly
step itself — splicing, deduplicating, producing the type:

| components | | templates | reflection | change |
|---|---|---|---|---|
| 128 | time   | 1.44 s  | 1.15 s | **−20 %** |
| 128 | memory | 142 MiB | 7 MiB  | **−95 %** |
| 256 | time   | 4.26 s  | 3.41 s | **−20 %** |
| 256 | memory | 569 MiB | 15 MiB | **−97 %** |

The memory cost of assembly nearly disappears: deduplicating a `std::vector<std::meta::info>` and
substituting once leaves nothing behind, while the template implementation creates — and the
compiler retains — a class specialization for every intermediate list. Time falls by about a fifth,
not by a factor: constant evaluation does the same quadratic membership work, just without
materializing types. Reflection is a cheaper representation here, not a better algorithm.

Method, full tables and caveats: [docs/benchmarks.md](docs/benchmarks.md).

## Requirements

C++23, for explicit object parameters — Clang 18+, GCC 14+, MSVC 19.40+. Header-only: copying
`include/tessera` into a project is a valid install.

Tested on Clang 21, Clang 22 (portable, fold and builtin backends), GCC 15, and the P2996 fork of
Clang for the reflection backend; CI covers Clang 18 and GCC 14 on every backend they can build.

## Building

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure

./build/examples/02_system_assembly     # the assembly example, end to end
./build/benchmarks/bench_runtime        # memory and dispatch measurements
python3 benchmarks/run_compile_bench.py --backends portable,builtin,fold
```

With CMake as a dependency:

```cmake
find_package(tessera REQUIRED)          # or add_subdirectory(external/tessera)
target_link_libraries(my_app PRIVATE tessera::tessera)
```

## Contributing

Contributions are accepted under the [Apache License 2.0](LICENSE) and require a
[Developer Certificate of Origin](DCO) sign-off (`git commit -s`). See
[CONTRIBUTING.md](CONTRIBUTING.md).

## License

Apache License 2.0 — see [LICENSE](LICENSE) and [NOTICE](NOTICE).

Copyright 2026 Denis Tishkov. Written by Denis Tishkov with Claude (Anthropic) as a
pair-programming tool.
