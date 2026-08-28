# Benchmarks

A library that does its work during translation has to be measured during translation. Two things
are measured here: what Tessera costs the compiler (time and memory), and what the design buys at
run time.

Everything is reproducible from the repository — no numbers in this file were typed by hand.

```bash
python3 benchmarks/run_compile_bench.py --compiler clang++ \
        --sizes 16,32,64,128,256 --backends portable,builtin,fold
cmake --build build --target tessera_bench_runtime && ./build/benchmarks/bench_runtime
```

## 1. Compile time and compiler memory

### Method

`benchmarks/compile_time/bench_tu.cpp` is compiled once per (implementation, component count,
deduplication backend). `benchmarks/run_compile_bench.py` forks the compiler directly and reads its
**own** resource usage with `os.wait4`, so peak RSS is the compiler process's exact high-water mark
rather than a sample. Four implementations of the same translation unit are compared:

| Implementation | What it compiles |
|---|---|
| `headers only` | the headers and an empty `main` — the toolchain's fixed cost |
| `std::tuple` | N components in a `std::tuple`, accessed by type with `std::get<T>` |
| `mosaic` | the same N components in a `tessera::mosaic`, accessed with `get<T>` |
| `assembly` | N systems declaring four overlapping dependencies each: 4N type mentions flat-mapped and deduplicated down to N, then assembled into a mosaic |

`std::tuple` versus `mosaic` isolates the cost of the container; `mosaic` versus `assembly` isolates
the cost of the deduplication, which is why `assembly` is the only row that varies by backend.

### Results

Clang 22.1.8, `-std=c++23 -O0`, best of one run per cell, x86-64 Linux.

#### Compile time (seconds)

| components | headers only | std::tuple | mosaic | assembly (portable) | assembly (builtin) | assembly (fold) |
|---|---|---|---|---|---|---|
|         16 |         0.41 |       0.41 |   0.40 |                0.47 |               0.45 |            0.48 |
|         32 |         0.35 |       0.52 |   0.50 |                0.66 |               0.60 |            0.65 |
|         64 |         0.34 |       0.83 |   0.77 |                1.23 |               0.95 |            1.31 |
|        128 |         0.34 |       1.47 |   1.56 |                3.15 |               2.08 |            3.10 |
|        256 |         0.34 |       3.60 |   4.87 |                9.23 |               5.86 |       **depth** |

#### Peak compiler memory (MiB, RSS)

| components | headers only | std::tuple | mosaic | assembly (portable) | assembly (builtin) | assembly (fold) |
|---|---|---|---|---|---|---|
|         16 |           92 |         96 |     92 |                  96 |                 94 |              95 |
|         32 |           91 |         98 |     94 |                 105 |                 98 |             105 |
|         64 |           91 |        113 |    103 |                 136 |                114 |             135 |
|        128 |           91 |        162 |    133 |                 268 |                157 |             239 |
|        256 |           91 |        315 |    242 |                 793 |                310 |       **depth** |

**depth** — the compilation fails: the linear fold needs one instantiation level per element and
runs past Clang's 1024-deep limit once the pre-deduplication list reaches ~1000 types (4N at
N = 256).

### Reading the numbers

* **The container is not the cost.** Up to 128 components a `mosaic` compiles within a few percent
  of a `std::tuple` of the same components; at 256 it is about a third slower in time and a quarter
  cheaper in memory. Whatever a project pays for assembly, it is not paying it for type-keyed
  storage.
* **Deduplication is the cost, and the backend matters.** At 256 components the builtin backend
  compiles the assembly in 5.9 s and 310 MiB against 9.2 s and 793 MiB for the portable one — 1.6×
  the time and 2.6× the memory for the library implementation of a single compiler builtin. This is
  the argument for keeping the backend replaceable.
* **The hybrid earns its place at the top end.** Below ~128 components the portable backend and the
  plain fold are within noise of each other, which is by design: the portable backend *is* the fold
  for lists up to 256 elements. Past that the fold stops compiling at all and the portable backend
  keeps going.
* **Everything is superlinear.** Doubling the component count roughly triples the assembly time.
  The design is meant for tens to low hundreds of components per translation unit; past that the
  answer is fewer types per translation unit, not a cleverer metafunction. For reference, the
  portable backend still compiles 512 components (≈20 s, 2.9 GiB) and 1024 (≈77 s, 11 GiB) — which
  is where you find out that the limit is memory, not correctness.

## 2. Run time and object memory

`benchmarks/runtime/bench_runtime.cpp`, built at `-O2`, Intel Core i7-3820, Clang 21.1.8. Both sides
of every comparison are fenced with the same optimization barrier, so neither loop can be folded
away.

### Memory (`sizeof`, bytes)

| composition | `tessera::mosaic` | `std::tuple` |
|---|---|---|
| 3 stateless policies | 1 | 1 |
| 2 stateless + 1 small | 4 | 4 |
| small + medium + `std::string` | 56 | 56 |
| 8 mixed components | 40 | 40 |

Identical, and that is the point: `[[no_unique_address]]` on each element means stateless
components — policies, tags, empty systems — cost nothing, exactly as with a tuple. Addressing
by type is free.

### System dispatch

Eight systems, 16 M calls.

| dispatch | ns/call |
|---|---|
| `mosaic::for_each` (assembled at compile time) | **0.49** |
| virtual calls through a `vector<unique_ptr<Interface>>` | 5.31 |

### Runtime value → compile-time constant

Eight op-codes, 8 M dispatches.

| dispatch | ns/lookup |
|---|---|
| `value_list::dispatch` | **2.05** |
| `unordered_map` of function pointers | 5.42 |

The comparison is not "templates are faster than virtual functions" in the abstract — it is that a
system set fixed at compile time needs no indirection to walk, and a constant known at compile
time needs no table to look up. Both numbers are what remains when the barrier prevents the
compiler from deleting the loop entirely.

## 3. Caveats

* Single-machine, single-configuration numbers; treat the ratios as the result and the absolute
  values as an artifact of this box.
* `-O0` for the compile-time benchmark: it measures the front end, which is where template work
  happens, and keeps the back end out of the picture.
* The compile-time table uses one run per cell (`--repeats 1`) because the front end is remarkably
  repeatable; the script defaults to three and takes the best.
