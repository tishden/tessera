# Static reflection: what changes, and the seam that is already in place

Tessera solves a problem that static reflection (P2996, voted into the C++26 working draft) solves
more directly. That is not a reason to wait — it is a reason to build the library so that the
transition is a change of *implementation*, not of interface. This document says what is already in
place, what will change, and what becomes possible that is out of reach today.

## Status today — a full implementation, not a backend

Reflection is no longer just a way to deduplicate a list. `detail::refl` implements **all five
operations** of the algebra — `unique_into`, `splice_unique_into`, `concat_into`, `select_into`,
`nth` — and selecting it (`-DTESSERA_ALGEBRA_BACKEND=REFLECTION`) makes every list operation in the
library go through `std::meta::info` instead of through template instantiation.

On the Bloomberg P2996 fork of Clang the whole suite passes with it selected, and
`tests/algebra_backends.test.cpp` asserts operation by operation that both implementations produce
*identical types*:

```
tessera 1.1.0 — algebra: reflection (P2996)
…
24 tests, 0 failed
```

Isolating the assembly step (total minus the cost of the mosaic itself, which is identical on both
implementations): at 256 components it costs **3.41 s and 15 MiB** of compiler memory against
**4.26 s and 569 MiB** for the template implementation — a fifth less time and about a fortieth of
the memory. Full tables in [benchmarks.md §1b](benchmarks.md).

### Getting a toolchain

No released compiler qualifies: Clang 22 and GCC 15 have neither the `^^` operator nor the header.

```bash
curl -fLO https://s3.amazonaws.com/compiler-explorer/opt/clang-bb-p2996-trunk-<date>.tar.xz
mkdir -p toolchain && tar -xf clang-bb-p2996-trunk-<date>.tar.xz -C toolchain --strip-components=2

export TESSERA_P2996_ROOT=$PWD/toolchain
cmake -S . -B build-reflection -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/clang-p2996.cmake \
      -DTESSERA_ALGEBRA_BACKEND=REFLECTION
cmake --build build-reflection && ctest --test-dir build-reflection --output-on-failure
```

The toolchain file exists because of a fourth trap, this one in the build system: no released CMake
knows C++26 for this compiler, so it silently settles on `-std=gnu++2b`, reflection switches off, and
the failure surfaces as a wall of errors from inside `<meta>` that reads like a library bug.
`cmake/toolchains/clang-p2996.cmake` clears `CMAKE_CXX_STANDARD_DEFAULT` so that CMake adds no `-std`
flag at all, and passes the standard, the reflection flag, the `libc++` selection, the
`-fconstexpr-steps` budget and the run-time `-rpath` explicitly.

`.github/workflows/reflection.yml` does the same on demand and weekly, so that the day the backend
stops matching the fork is the day CI says so — rather than the day someone tries it.

### Traps worth writing down

They are worth writing down, because each one costs an afternoon to rediscover:

1. **No feature-test macro.** The fork defines neither `__cpp_impl_reflection` nor
   `__cpp_reflection`; it only answers `__has_feature(reflection)`, and only with
   `-freflection-latest`. `config.hpp` therefore probes all three spellings.

2. **A splice operand must be a constant expression, and a `consteval` call written inline is not
   one.** `using T = [:std::meta::substitute(^^type_list, unique_metas({^^Ts...})):];` is rejected;
   naming the result first — a `consteval` function returning `std::meta::info`, or a `constexpr`
   variable template — is accepted.

3. **Feature probes must not call `consteval` functions.** The natural probe for the P2996R10
   access-context parameter, `requires { nonstatic_data_members_of(^^T, access_context::current()); }`,
   is always false: a `consteval` call inside a requires-expression is not a constant expression
   there. Probing for the *type* (`requires { typename std::meta::access_context; }`) works.

Also: Clang's constant-evaluation budget (`-fconstexpr-steps`) has to be raised for long lists, since
the deduplication that used to be template instantiations is now an ordinary quadratic loop.

## The seam

Two things make the switch a local change:

1. **Five operations, one namespace alias.** Everything `type_list` and `mosaic` do with lists goes
   through `unique_into`, `splice_unique_into`, `concat_into`, `select_into` and `nth`, in
   `tessera::detail::ops`. `detail::tmpl` implements them with templates, `detail::refl` with
   reflection, and `algebra.hpp` picks one. Neither public type mentions either implementation.

2. **No compiler-specific spelling outside `config.hpp`.** The probes for reflection sit next to the
   probes for Clang's builtins, so a toolchain that ships P2996 selects the reflection
   implementation the same way Clang 22 selects the builtin one.

The operations take the *target template*, not just the element types, which is what lets the
reflection implementation collapse a whole pipeline into one substitution:

```cpp
template<template<class...> class Target, class... Ls>
consteval std::meta::info splice_unique_into_info() {
    std::vector<std::meta::info> kept;
    const auto splice = [&kept](std::meta::info list) {
        for (const std::meta::info element : std::meta::template_arguments_of(list)) {
            push_unique(kept, element);
        }
    };
    (splice(^^Ls), ...);
    return std::meta::substitute(^^Target, kept);
}

template<template<class...> class Target, class... Ls>
using splice_unique_into = [:splice_unique_into_info<Target, Ls...>():];
```

`tessera::of<A, B, C>` is exactly that call with `Target = mosaic`: splicing, deduplication and the
mosaic itself in one step. The template implementation of the same operation is a concatenation
plan, a hybrid deduplicator and a conversion — every stage of which leaves a class specialization
behind for the compiler to keep.

What does *not* move: a user predicate like `Predicate<T>::value` is a template, and the compiler
instantiates it once per element either way. `filter` therefore hands the algebra a bit mask rather
than the predicate.

## How the backend is kept honest

The reflection implementation is compiled **whenever the toolchain has reflection**, not only when
it is selected. That is what lets `tests/algebra_backends.test.cpp` instantiate both implementations
side by side and assert, operation by operation, that they produce the *identical type* — the
property every claim in this document rests on. Selecting the implementation
(`-DTESSERA_ALGEBRA_BACKEND=REFLECTION`) only changes which namespace `detail::ops` names.

## What gets better

| Area | Today | With reflection |
|---|---|---|
| Deduplication | hybrid fold + base-class set, superlinear compile time, depth-bounded | a `consteval` loop over a vector, no instantiation depth |
| Concatenation, filtering | an index plan plus a pack expansion per operation | plain algorithms over `std::vector<std::meta::info>` |
| `type_name<T>()` | parsing `__PRETTY_FUNCTION__` / `__FUNCSIG__` | `std::meta::display_string_of` |
| Diagnostics | a mangled type in a `static_assert` message | the actual names, formatted |
| Ordering | no way to sort types | sort by name, size, alignment — a canonical element order becomes possible |
| Dependency resolution (`graph.hpp`) | a depth-first walk over `type_list`, identical on every implementation | unchanged — the walk is built from `contains`/`append`, not from the five operations, so there is nothing for reflection to take over |

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

## Acceptance criteria, and where they stand

1. ✅ the full test suite passes with `TESSERA_ALGEBRA_BACKEND=REFLECTION` and produces types
   identical to the template implementation — asserted operation by operation in
   `tests/algebra_backends.test.cpp`, which compares exact types rather than sizes;
2. ✅ `benchmarks/run_compile_bench.py --backends portable,fold,reflection` runs, and the numbers
   are in [benchmarks.md §1b](benchmarks.md);
3. ✅ `type_name` uses `display_string_of` where reflection is available, keeping the
   string-parsing fallback for every other toolchain;
4. ✅ `members_of_t` is covered by `tests/reflect.test.cpp`, including the aggregate-to-mosaic path;
5. ✅ all five operations of the algebra are implemented, not just deduplication.

What keeps the implementation behind `TESSERA_ENABLE_REFLECTION_BACKEND` regardless: it is validated
against one implementation of a proposal that is still moving. It is not selected automatically even
where it compiles, and it will not be until a released toolchain ships the standard spelling.
