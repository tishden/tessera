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

## Deduplication backends

Deduplicating a type list is the expensive part of assembly, so it is a replaceable component with
one interface and four implementations, selected automatically:

| Backend | Requirement | What it does |
|---|---|---|
| `PORTABLE` (default) | any C++23 compiler | a fold for short lists, divide and conquer above it, membership through a base-class table |
| `BUILTIN` | Clang 22+ | one `__builtin_dedup_pack` expansion |
| `REFLECTION` | P2996 (`<meta>` or `<experimental/meta>`) | filters `std::meta::info` as ordinary constexpr data — verified on the P2996 fork of Clang, a third less compiler memory at 128 components ([docs/reflection.md](docs/reflection.md)) |
| `FOLD` | any C++23 compiler | the textbook linear fold, kept as the benchmark reference |

Every compiler-specific spelling in the library lives behind a macro in
[`config.hpp`](include/tessera/config.hpp) — Clang builtins are guarded by `__clang__`, and nothing
outside that header mentions a vendor. Pin a backend with `-DTESSERA_DEDUP_BACKEND=PORTABLE` (CMake)
when you want to measure or reproduce.

Every backend the toolchain can compile is compiled, whether or not it is the one in use, so
`tests/dedup_backends.test.cpp` can instantiate all of them on the same input and assert they
produce the *identical type*. That is the property that makes the substitution safe, and it is
checked rather than assumed.

## Requirements

C++23, for explicit object parameters — Clang 18+, GCC 14+, MSVC 19.40+. Header-only: copying
`include/tessera` into a project is a valid install.

Tested here on Clang 21, Clang 22 (every backend) and GCC 15; the CI matrix additionally covers
Clang 18, Clang 19 and GCC 14.

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

## Origin

Tessera is the extracted and rewritten form of the compile-time assembly layer of a production
low-latency system, where it builds the component graph of every binary — services, adapters, the
systems that use them — before the program runs. The library keeps that functionality and replaces
the parts that were specific to that codebase: the deduplication is no longer tied to one compiler's
builtin, the algebra is testable on its own, and the API follows standard-library conventions.

## Contributing

Contributions are accepted under the [Apache License 2.0](LICENSE) and require a
[Developer Certificate of Origin](DCO) sign-off (`git commit -s`). See
[CONTRIBUTING.md](CONTRIBUTING.md).

## License

Apache License 2.0 — see [LICENSE](LICENSE) and [NOTICE](NOTICE).

Copyright 2026 Denis Tishkov. Written by Denis Tishkov with Claude (Anthropic) as a
pair-programming tool.
