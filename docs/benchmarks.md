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
|         16 |         0.34 |       0.38 |   0.37 |                0.47 |               0.45 |            0.47 |
|         32 |         0.33 |       0.51 |   0.47 |                0.65 |               0.57 |            0.64 |
|         64 |         0.32 |       0.80 |   0.74 |                1.18 |               0.95 |            1.19 |
|        128 |         0.33 |       1.45 |   1.50 |                2.93 |               2.07 |            2.87 |
|        256 |         0.33 |       3.46 |   4.54 |                8.98 |               5.71 |       **depth** |

#### Peak compiler memory (MiB, RSS)

| components | headers only | std::tuple | mosaic | assembly (portable) | assembly (builtin) | assembly (fold) |
|---|---|---|---|---|---|---|
|         16 |           90 |         98 |     94 |                  95 |                 95 |              95 |
|         32 |           92 |         98 |     94 |                 105 |                100 |             105 |
|         64 |           92 |        114 |    101 |                 136 |                113 |             135 |
|        128 |           92 |        162 |    128 |                 270 |                157 |             241 |
|        256 |           91 |        316 |    227 |                 793 |                305 |       **depth** |

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

## 1b. The reflection implementation, measured

The reflection implementation cannot be built by any released compiler, so it gets its own run on
the reference implementation — the Bloomberg P2996 fork of Clang, as published by Compiler Explorer.
That build is based on Clang 21 and has no `__builtin_dedup_pack`, so the comparison here is
reflection against the two template implementations on one toolchain:

```bash
python3 benchmarks/run_compile_bench.py \
        --compiler <toolchain>/bin/clang++ --std c++26 --sizes 16,32,64,128,256 \
        --backends portable,fold,reflection --repeats 1 \
        --extra "-O0 -stdlib=libc++ -freflection-latest -fconstexpr-steps=1000000000"
```

#### Compile time (seconds) — clang-p2996 trunk 2026-08-08

| components | headers only | std::tuple | mosaic | assembly (portable) | assembly (fold) | assembly (reflection) |
|---|---|---|---|---|---|---|
|         16 |         1.38 |       0.27 |   1.40 |                1.52 |            1.51 |                  1.53 |
|         32 |         1.31 |       0.33 |   1.45 |                1.73 |            1.70 |                  1.73 |
|         64 |         1.32 |       0.52 |   1.74 |                2.23 |            2.23 |                  2.23 |
|        128 |         1.36 |       0.84 |   2.55 |                3.99 |            3.84 |              **3.70** |
|        256 |         1.38 |       1.85 |   5.59 |                9.85 |           depth |              **9.00** |

#### Peak compiler memory (MiB)

| components | headers only | std::tuple | mosaic | assembly (portable) | assembly (fold) | assembly (reflection) |
|---|---|---|---|---|---|---|
|         16 |          126 |        101 |    136 |                 133 |             136 |                   129 |
|         32 |          126 |        105 |    136 |                 143 |             142 |               **133** |
|         64 |          134 |        112 |    139 |                 174 |             173 |               **142** |
|        128 |          134 |        146 |    166 |                 308 |             277 |               **173** |
|        256 |          126 |        279 |    267 |                 836 |           depth |               **282** |

### What the assembly step itself costs

The `mosaic` column is the same work on every implementation — instantiating N slots and N accessor
calls — so subtracting it isolates the part that actually changed hands: splicing, deduplicating and
producing the type.

| components | | templates | reflection | change |
|---|---|---|---|---|
| 128 | time   | 1.44 s  | 1.15 s | **−20 %** |
| 128 | memory | 142 MiB | 7 MiB  | **−95 %** |
| 256 | time   | 4.26 s  | 3.41 s | **−20 %** |
| 256 | memory | 569 MiB | 15 MiB | **−97 %** |

That is the shape of the result, and it is worth being precise about what it means:

* **The memory cost of assembly nearly disappears.** Deduplicating a `std::vector<std::meta::info>`
  and substituting once leaves nothing behind; the template implementation creates a class
  specialization for every intermediate list, and the compiler keeps all of them. At 256 components
  that is the difference between 569 MiB and 15 MiB, and the gap widens with N.
* **Time falls by about a fifth, not by a factor.** Constant evaluation is doing the same quadratic
  membership work the fold does, just without materializing types. Reflection is not a faster
  algorithm here — it is a cheaper representation.
* **The remaining cost is the container, not the algebra.** At 256 components the mosaic alone is
  5.59 s of the 9.00 s total. Making the algebra free would not make the translation unit cheap;
  fewer components per translation unit would.
* **`headers only` costs 1.3 s on this toolchain** against 0.27 s for the `std::tuple` translation
  unit, because including `<meta>` and `<vector>` from this libc++ dominates a small TU. Compare
  columns against that baseline, not against the Clang 22 tables above — different compiler,
  different standard library.
* Clang's constant-evaluation budget has to be raised (`-fconstexpr-steps`); at the default the
  compiler reports the splice operand as "not a constant expression" once the list gets long.

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
