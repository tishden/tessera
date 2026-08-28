# Static reflection: what changes, and the seam that is already in place

Tessera solves a problem that static reflection (P2996, voted into the C++26 working draft) solves
more directly. That is not a reason to wait — it is a reason to build the library so that the
transition is a change of *implementation*, not of interface. This document says what is already in
place, what will change, and what becomes possible that is out of reach today.

## Status today

`TESSERA_HAS_REFLECTION` is 1 when the compiler defines `__cpp_impl_reflection` (the standard
spelling) or `__cpp_reflection` (the P2996 reference implementation) *and* one of `<meta>` or
`<experimental/meta>` is available. The header that was found is exposed as
`TESSERA_REFLECTION_HEADER`, so no other file repeats the probe.

No released toolchain satisfies that: Clang 22 and GCC 15 have neither the `^^` operator nor the
header. The implementation to build against is the Bloomberg P2996 fork of Clang; the practical way
to get a binary of it is the build that Compiler Explorer publishes:

```bash
curl -fLO https://s3.amazonaws.com/compiler-explorer/opt/clang-bb-p2996-trunk-<date>.tar.xz
mkdir -p toolchain && tar -xf clang-bb-p2996-trunk-<date>.tar.xz -C toolchain --strip-components=1

cmake -S . -B build-reflection \
      -DCMAKE_CXX_COMPILER=$PWD/toolchain/bin/clang++ \
      -DCMAKE_CXX_FLAGS="-freflection-latest -stdlib=libc++" \
      -DTESSERA_DEDUP_BACKEND=REFLECTION
cmake --build build-reflection && ctest --test-dir build-reflection --output-on-failure
```

`.github/workflows/reflection.yml` does exactly this on demand and weekly, so that the day the
backend stops matching the fork is the day CI says so — rather than the day someone tries it.

## The seam

Two things make the switch a local change:

1. **One deduplication interface.** Everything in the library that removes duplicates goes through
   `detail::deduplicated_t<Ts...>` in `dedup.hpp`. The implementations sit behind it, selected by
   `TESSERA_DEDUP_BACKEND` in `config.hpp`. Adopting reflection means finishing one of them; no
   other header changes.

2. **No compiler-specific spelling outside `config.hpp`.** The probes for reflection sit next to the
   probes for Clang's builtins, so a toolchain that ships P2996 selects the reflection backend the
   same way Clang 22 selects the builtin one.

The backend as it stands:

```cpp
consteval auto unique_metas(std::vector<std::meta::info> metas) -> std::vector<std::meta::info> {
    std::vector<std::meta::info> kept;
    for (const std::meta::info meta : metas) {
        bool seen = false;
        for (const std::meta::info already : kept) {
            seen = seen || (already == meta);
        }
        if (!seen) {
            kept.push_back(meta);
        }
    }
    return kept;
}

template<class... Ts>
using unique_reflection_t = [:std::meta::substitute(^^type_list, unique_metas({^^Ts...})):];
```

Twelve lines of ordinary code replace the hybrid fold, the base-class membership table and the
index-plan concatenation — because with reflection a type list is *data*, and the compiler's
constant evaluator is a better interpreter than the template instantiation machinery. It also lifts
the algorithmic limits described in [design.md](design.md): a `std::vector` of `info` can be sorted
and deduplicated with no instantiation depth at all.

## How the backend is kept honest

The reflection implementation is compiled **whenever the toolchain has reflection**, not only when
it is the selected backend. That is what lets `tests/dedup_backends.test.cpp` instantiate it next to
the fold, the hybrid and the Clang builtin on the same input and assert that all of them produce the
*identical type* — the property every claim in this document rests on. Selecting the backend
(`-DTESSERA_DEDUP_BACKEND=REFLECTION`) only changes which one `detail::deduplicated_t` forwards to.

## What gets better

| Area | Today | With reflection |
|---|---|---|
| Deduplication | hybrid fold + base-class set, superlinear compile time, depth-bounded | a `consteval` loop over a vector, no instantiation depth |
| Concatenation, filtering | an index plan plus a pack expansion per operation | plain algorithms over `std::vector<std::meta::info>` |
| `type_name<T>()` | parsing `__PRETTY_FUNCTION__` / `__FUNCSIG__` | `std::meta::display_string_of` |
| Diagnostics | a mangled type in a `static_assert` message | the actual names, formatted |
| Ordering | no way to sort types | sort by name, size, alignment — a canonical element order becomes possible |

## What becomes newly possible

* **Mosaics from aggregates.** `members_of_t<Aggregate>` (sketched in `reflect.hpp`) reads the
  member types off an existing struct, so a hand-written configuration aggregate can be turned into
  a type-addressable mosaic without restating its members.
* **Name-based access.** `get<"frameBuffer">()` alongside `get<FrameBuffer>()`, resolved through the
  reflected member names.
* **Generated diagnostics.** A missing component today produces "T is not an element of this
  mosaic"; with reflection it can list what *is* in the mosaic and what declared the requirement.
* **Layout control.** Sorting elements by alignment before laying them out, without changing the
  type-keyed interface — a mosaic could pack better than the declaration order allows.

## Acceptance criteria for finishing the backend

The reflection path is done when, on a toolchain that ships P2996:

1. the full test suite passes with `TESSERA_DEDUP_BACKEND=REFLECTION` and produces types identical
   to the portable backend (the suite already asserts exact types, not just sizes);
2. `benchmarks/run_compile_bench.py --backends portable,builtin,reflection` runs, and the numbers go
   into [benchmarks.md](benchmarks.md) next to the existing columns;
3. `type_name` switches to `display_string_of` with the string-parsing fallback kept for older
   toolchains;
4. `members_of_t` gets tests of its own, and the aggregate-to-mosaic path gets an example.

Until all four hold, the backend stays behind `TESSERA_ENABLE_REFLECTION_BACKEND` and is documented
as experimental.
