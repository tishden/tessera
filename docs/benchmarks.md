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

Section 1 measures the range the library is for; [section 1c](#1c-scaling-where-each-implementation-stops)
pushes both implementations of the algebra until they stop, and says what stops them.

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

## 1c. Scaling: where each implementation stops

Sections 1 and 1b measure the range the library is designed for — components in the tens to low
hundreds. This section asks the opposite question: pushed as far as the toolchain allows, what
actually stops each implementation of the algebra, and at what size?

### Method

The realistic `assembly` workload is bounded by the container, not by the algebra: a mosaic of N
elements is N base classes and N accessor instantiations, and that dominates long before
deduplication does. Two implementations of the translation unit isolate the algebra instead:

| Implementation | What it compiles |
|---|---|
| `setup` | N component types and the 4N mentions of them collected into a `type_list`, **without deduplication** |
| `algebra` | the same 4N mentions, deduplicated into a `type_list` — `unique_into` and nothing else |

`algebra` minus `setup` is therefore the cost of deduplication alone, with the cost of instantiating
the types themselves subtracted out. Nothing is constructed, no container is built, and no object
exists at run time.

```bash
python3 benchmarks/run_compile_bench.py --compiler <p2996>/bin/clang++ --std c++26 \
        --only setup,algebra --sizes 250,500,1000,2000,4000,8000,16383 \
        --backends portable,reflection --repeats 1 \
        --timeout 1800 --memory-cap 10000 --stack -1 \
        --extra="-O0 -stdlib=libc++ -freflection-latest -fconstexpr-steps=4000000000 \
                 -fbracket-depth=131072"
```

### Results

clang-p2996 trunk 2026-09-03 (Clang 21 base), `-std=c++26 -O0`, one run per cell. **Totals**, with
the `setup` column being the floor every other column includes:

#### Compile time (seconds)

| N | type mentions | setup | templates (portable) | reflection (linear scan) | reflection (hashed) |
|---|---|---|---|---|---|
|   250 |  1 000 | 1.45 |    5.36 |    3.01 |    9.42 |
|   500 |  2 000 | 1.45 |   14.43 |    8.02 |   17.43 |
| 1 000 |  4 000 | 1.55 |   45.24 |   28.50 |   33.06 |
| 2 000 |  8 000 | 1.80 | *stopped at 21 GiB* |  110.40 |   68.37 |
| 4 000 | 16 000 | 2.25 | — |  437.03 |  137.55 |
| 8 000 | 32 000 | 3.26 | — | 1773.04 |  277.51 |
|16 383 | 65 532 | 5.46 | — | *≈7 400, extrapolated* |  584.64 |

#### Peak compiler memory (MiB)

| N | type mentions | setup | templates (portable) | reflection (linear scan) | reflection (hashed) |
|---|---|---|---|---|---|
|   250 |  1 000 | 127 |  621 | 126 |  168 |
|   500 |  2 000 | 131 | 2123 | 129 |  212 |
| 1 000 |  4 000 | 139 | 7996 | 137 |  300 |
| 2 000 |  8 000 | 156 | *stopped at 21 504* | 150 |  479 |
| 4 000 | 16 000 | 191 | — | 179 |  836 |
| 8 000 | 32 000 | 261 | — | 240 | 1556 |
|16 383 | 65 532 | 418 | — | — | 3071 |

The template column stops at N = 1 000 on purpose. At N = 2 000 the compiler passed 21 GiB without
finishing and was killed rather than driven into the machine's limits; the growth below is clean
enough to say what would have happened without watching it happen.

#### The cost of deduplication alone (`algebra` minus `setup`)

| N | mentions | templates | | reflection, scan | | reflection, hashed | |
|---|---|---|---|---|---|---|---|
| | | time | memory | time | memory | time | memory |
|   250 |  1 000 |   3.91 s |  494 MiB |    1.56 s | −1 MiB |   7.97 s |   41 MiB |
|   500 |  2 000 |  12.98 s | 1992 MiB |    6.57 s | −2 MiB |  15.98 s |   81 MiB |
| 1 000 |  4 000 |  43.69 s | 7857 MiB |   26.95 s | −2 MiB |  31.51 s |  161 MiB |
| 2 000 |  8 000 | — | — |  108.60 s | −6 MiB |  66.57 s |  323 MiB |
| 4 000 | 16 000 | — | — |  434.78 s | −12 MiB | 135.30 s |  645 MiB |
| 8 000 | 32 000 | — | — | 1769.78 s | −21 MiB | 274.25 s | 1295 MiB |
|16 383 | 65 532 | — | — | — | — | 579.18 s | 2653 MiB |

### Reading the numbers

* **Deduplicating with reflection costs the compiler no memory at all.** Not "less" — none that can
  be measured. The peak of the `algebra` translation unit is the peak of the `setup` translation
  unit, to within ±21 MiB across a 32× range, and the sign of the difference is as often negative as
  positive. The template implementation over the same range goes 494 → 1992 → 7857 MiB, ×3.94 to
  ×4.03 per doubling: exactly quadratic, exactly as many retained class specializations as the
  algorithm creates. This supersedes §1b: the 15 MiB measured there was not a small cost but the
  noise floor of a comparison that still had the mosaic's own allocation in both sides. It is also
  specific to this compiler — see §1e, where GCC 16 runs the same experiment and the memory does
  *not* disappear.
* **Both are quadratic in time, and reflection is quadratic with a smaller constant.** The scan path
  goes ×4.21, ×4.10, ×4.03, ×4.00, ×4.07 per doubling — textbook. It is doing the same membership
  test the template implementation does; it just is not materialising a type for each intermediate
  result. This is the same conclusion §1b reaches at 128 and 256 components, holding four doublings
  further out.
* **Membership is the whole cost, and with reflection it is ordinary code.** Replacing the linear
  scan with an open-addressed table keyed on a hash of `display_string_of` makes deduplication
  exactly linear — 8.0 to 8.8 ms and 41.3 to 41.5 KiB per type mention, steady from 1 000 mentions
  to 65 532. At the ceiling that is 579 s against roughly 7 400 s extrapolated for the scan.
  Verified to produce types identical to the template implementation past the threshold where the
  branch is taken.
* **But it is not the right default, and the numbers say why.** Hashing costs about 660 comparisons'
  worth of interpreter time per element, so it only overtakes the scan past ~5 000 mentions
  (N ≈ 1 300) — an order of magnitude beyond the range this library is for — and it pays for the
  speed in exactly the resource reflection was winning on: 2 653 MiB at the ceiling, against zero.
  `detail::refl` therefore keeps the linear scan. The measurement is here because the *possibility*
  is the point: on the template side the cost is the compiler creating types and cannot be
  programmed around, while on the reflection side the algorithm is ordinary code and can simply be
  replaced when a workload justifies it.

### The four walls, in the order you hit them

Every one of these was hit while producing the table above, and three of the four are raised by a
compiler default rather than by anything the library does.

1. **A fold expression over ~2 048 arguments** — `error: instantiating fold expression with 4000
   arguments exceeded expression nesting limit of 2048`. Both implementations expand a pack with a
   fold, so both stop at N = 512 until `-fbracket-depth` is raised. Nothing about the algebra
   changes; the flag does.
2. **The compiler's stack, at roughly 12 000 mentions** — `clang` dies of `SIGSEGV` after four
   seconds and 165 MiB, printing nothing at all. It is the parser recursing over the fold, and
   `ulimit -s unlimited` (the benchmark's `--stack -1`) removes it. A crash with no diagnostic is
   easy to misread as a library bug, which is why the benchmark classifies it as `crash` rather than
   `failed`.
3. **Compiler memory, for the template implementation, at ~4 000 mentions.** 7.9 GiB, growing ×4 per
   doubling. This one is real, is the library's own, and is the reason the reflection path exists.
4. **65 535 type mentions, on Clang, absolutely.** Past that the front end cannot represent the list
   — see below. Reflection does not help: the input arrives as a template argument pack either way.

### A hard ceiling worth knowing about: `sizeof...` overflows silently

Past a certain pack size Clang returns a **wrong number** from `sizeof...`, with no diagnostic. Pack
*expansion* stays correct; only the count is wrong. Measured on Clang 21.1.8 and on the P2996 fork:

| pack size | `sizeof...` over a type pack | `sizeof...` over a non-type pack |
|---|---|---|
| 32 767 | 32 767 | 32 767 |
| 32 768 | 32 768 | **0** |
| 65 535 | 65 535 | **32 767** |
| 65 536 | **0** | **0** |
| 100 000 | **34 464** | **1 696** |

The counts wrap modulo 65 536 for type packs and modulo 32 768 for non-type packs. GCC 11.5 computes
every one of these correctly, so this is Clang's ceiling and not the language's. It is
[LLVM #119600](https://github.com/llvm/llvm-project/issues/119600), open since December 2024 and
labelled a miscompilation and a regression since Clang 16; the thresholds above are not in the
report.

For Tessera this means a list of at most **65 535 type mentions** per operation on Clang — N ≤ 16 383
at four dependencies per component — and it means N = 100 000 and N = 1 000 000 are not measurable
at all rather than merely expensive. The `static_assert` on the list length in
`benchmarks/compile_time/bench_tu.cpp` is the only thing that catches the truncation; without it the
benchmark would silently measure a list of 6 784 elements and report it as 400 000. Any code that
computes with very large packs wants the same guard.

## 1d. What following the graph costs

`of<...>` flattens a list that is already complete; `resolve<...>` follows each type's declared
dependencies transitively and topologically sorts the result. The second does strictly more, so the
question is what the extra properties cost.

### Method

Two workloads of the same size, both ending in a list of N distinct types:

| Implementation | What it compiles |
|---|---|
| `assembly` | N systems declaring four overlapping dependencies each — 4N mentions flat-mapped and deduplicated into a mosaic. No edges are followed |
| `resolve` | N services in a DAG, service `I` depending on `I/2`, `I/3` and `I/5` — 3N edges, depth `log N`, most nodes reachable by several paths. Resolved transitively and topologically sorted |

The `resolve` translation unit also asserts `is_topologically_sorted` on the result, so the ordering
property is established by the same compilation that is being timed.

```bash
python3 benchmarks/run_compile_bench.py --compiler clang++ \
        --only "headers only,mosaic,assembly,resolve" --sizes 16,32,64,128,256 \
        --backends portable --repeats 3
```

### Results

Clang 21.1.8, `-std=c++23 -O0`, best of three. **Totals**; `mosaic` is the floor both of the last two
columns include, since both end by building a mosaic of N elements.

| N | headers only | mosaic | assembly | resolve |
|---|---|---|---|---|
|  16 | 0.25 s / 94 MiB | 0.30 s /  97 MiB | 0.35 s / 101 MiB | 0.30 s /  98 MiB |
|  32 | 0.25 s / 94 MiB | 0.35 s /  98 MiB | 0.50 s / 107 MiB | 0.40 s / 103 MiB |
|  64 | 0.25 s / 94 MiB | 0.50 s / 106 MiB | 0.85 s / 134 MiB | 0.60 s / 116 MiB |
| 128 | 0.25 s / 94 MiB | 1.05 s / 132 MiB | 2.05 s / 268 MiB | 1.35 s / 168 MiB |
| 256 | 0.25 s / 94 MiB | 3.06 s / 233 MiB | 6.01 s / 791 MiB | 4.16 s / 375 MiB |

Net of the mosaic, i.e. the assembly step alone:

| N | assembly | | resolve | |
|---|---|---|---|---|
| | time | memory | time | memory |
|  64 | 0.35 s |  28 MiB | 0.10 s |  10 MiB |
| 128 | 1.00 s | 136 MiB | 0.30 s |  36 MiB |
| 256 | 2.95 s | 558 MiB | 1.10 s | 142 MiB |

### Reading the numbers

* **Following the graph is cheaper than flattening a flat list**, by 2.7× in time and 3.9× in memory
  at 256 services — which is not the result one expects from the operation that does more.
* **The reason is what each one asks the compiler to build.** Deduplication rebuilds the list: every
  intermediate is a class specialization the compiler creates, names and keeps, and there are
  O(4N) of them. The traversal asks a different question — `type_list::contains` is a fold
  expression over a list that already exists, so a membership test creates no list at all. Only the
  `append` at each emitted node builds one, and there are N of those rather than 4N.
* **Both are still superlinear**, and for the same reason as everything else in this document: the
  appends grow. Doubling N roughly triples the resolution cost, which is the same shape as the rest
  of the library and puts the practical range in the same place — services in the tens to low
  hundreds per translation unit.
* **Depth is spent per list element, not per level of the graph** — see the ceilings below. The
  benchmark graph being `log N` deep is not what keeps it inside the limit; the lists it folds over
  being short is.

### How far resolution goes, and what stops it

The traversal folds over lists recursively — `walk_list` recurses into the tail — so it spends one
instantiation level per element of every list on the current stack. That, and not the size of the
graph, is what runs out. Measured on Clang 21.1.8 at the default `-ftemplate-depth=1024`:

| graph shape | ceiling | what fails |
|---|---|---|
| a chain, depth equal to size | **510 links** (511 fails) | ~2 levels per link |
| one root with a wide dependency list | **~1010 direct dependencies** (1000 compiles, 1020 fails) | ~1 level per element |
| N roots over a DAG of `log N` depth | **512 roots** (1024 fails) | ~2 levels per root |

All three fail the same way — `recursive template instantiation exceeded maximum depth of 1024` —
which at least points at itself, unlike most of the walls in §1c.

Cost of the DAG shape up to that ceiling:

| N services | time | compiler memory |
|---|---|---|
|  64 |  0.60 s |  116 MiB |
| 128 |  1.35 s |  168 MiB |
| 256 |  4.16 s |  375 MiB |
| 512 | 14.23 s | 1161 MiB |
| 1024 | *depth* | — |

Raising `-ftemplate-depth` does work, with `ulimit -s unlimited` alongside it — without the larger
stack the compiler segfaults instead of diagnosing. A chain of 1000 services then compiles in
**80.74 s and 5.3 GiB**, which is the number that says a chain is the wrong shape rather than a
bigger budget being the answer: 1000 services in the `log N` DAG never get near that.

The practical reading: keep the graph shallow and the root list short, and the ceiling is nowhere
near. A hundred services in a shallow graph cost 1.35 s at N = 128 and never approach the depth
limit; a hundred services in a chain would.

### The header costs nothing to those who do not use it

`graph.hpp` is included by the umbrella header, so it is worth checking that adding it did not tax
every translation unit that never resolves anything. Measured with and without it on the same
compiler (`--include` points the benchmark at a second copy of the headers):

| | headers only | mosaic (256) | assembly (256) |
|---|---|---|---|
| without `graph.hpp` | 0.30 s / 94 MiB | 2.91 s / 233 MiB | 6.16 s / 791 MiB |
| with `graph.hpp`    | 0.25 s / 94 MiB | 3.01 s / 233 MiB | 6.11 s / 792 MiB |

Identical within run-to-run noise: the traversal is templates that nothing instantiates until
`resolve` is named.

## 1e. The same measurement on GCC 16 — and why the memory result does not travel

GCC 16.1 (April 2026) is the first *released* compiler to implement P2996, behind `-freflection`.
That makes the reflection backend buildable without an experimental fork, and it makes §1c's headline
result checkable on a second implementation. It does not survive the check.

```bash
python3 benchmarks/run_compile_bench.py --compiler <gcc-16>/bin/g++ --std c++26 \
        --only setup,algebra --sizes 250,500,1000 --backends portable,reflection --repeats 1 \
        --extra="-O0 -freflection -fconstexpr-ops-limit=4000000000 -ftemplate-depth=16384"
```

GCC 16.2.0, net of `setup`, so this is the price of deduplication alone:

| type mentions | templates | | reflection | | reflection's advantage |
|---|---|---|---|---|---|
| | time | memory | time | memory | |
| 1 000 |  2.66 s |  226 MiB |  2.11 s |  119 MiB | 1.26× time, 1.90× memory |
| 2 000 | 10.17 s |  798 MiB |  8.72 s |  470 MiB | 1.17× time, 1.70× memory |
| 4 000 | 45.29 s | 2773 MiB | 34.42 s | 1898 MiB | 1.32× time, 1.46× memory |

The same rows on clang-p2996, from §1c:

| type mentions | templates | | reflection | |
|---|---|---|---|---|
| 1 000 |  3.91 s |  494 MiB |  1.56 s | **−1 MiB** |
| 2 000 | 12.98 s | 1992 MiB |  6.57 s | **−2 MiB** |
| 4 000 | 43.69 s | 7857 MiB | 26.95 s | **−2 MiB** |

### Reading the two together

**"Deduplication through reflection costs no memory" is a property of Clang's implementation, not of
reflection.** On GCC 16 the reflection path allocates 119 → 470 → 1898 MiB across the same range,
growing ×3.95 and ×4.04 per doubling — quadratic, exactly like the template path beside it. Clang's
fork retains nothing measurable; GCC's constant evaluator evidently retains the `std::meta::info`
vectors much as the template implementation retains class specializations.

**And the advantage shrinks with N**: 1.90× → 1.70× → 1.46×. Two curves with the same exponent and
different constants converge in ratio as the constants stop mattering. On Clang the exponent itself
differs — one curve is quadratic and the other is flat — which is why that gap widens instead.

**What holds on both compilers** is the claim the numbers were collected for: reflection is a cheaper
*representation* for the same algorithm, not a better algorithm. Deduplication stays quadratic in
time everywhere, on both implementations, on both compilers. What varies is how much of the
intermediate state the compiler chooses to keep — and that turns out to be an implementation quality
question, currently answered far better by the Bloomberg fork than by GCC.

**Do not compare the absolute numbers across the two tables.** Different front ends: GCC's *template*
path is itself about three times cheaper in memory than Clang's over this range (226 against 494 MiB
at 1 000 mentions, 2773 against 7857 at 4 000). Only the within-toolchain ratios and the growth
exponents mean anything here.

The practical reading for anyone choosing today: reflection is worth having on either compiler, and
on GCC 16 it buys a constant factor rather than an asymptote. If the reason you want it is that
compiler memory is what kills your CI, check it on your compiler before believing a benchmark —
including this one.

## 2. Run time and object memory

`benchmarks/runtime/bench_runtime.cpp`, built at `-O2`, Intel Core i7-3820 @ 3.60 GHz, Clang 21.1.8.
Both sides of every comparison are fenced with the same optimization barrier, so neither loop can be
folded away. Best of seven runs, matching the convention of the compile-time tables; the spread
across those runs is under 5 % for every row, and the `unordered_map` row is the widest of them.

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
| virtual calls through a `vector<unique_ptr<Interface>>` | 5.12 |

### Runtime value → compile-time constant

Eight op-codes, 8 M dispatches.

| dispatch | ns/lookup |
|---|---|
| `value_list::dispatch` | **2.00** |
| `unordered_map` of function pointers | 5.13 |

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
