# Contributing

Thanks for looking. Bug reports, questions and patches are all welcome.

## Developer Certificate of Origin

This project uses the [Developer Certificate of Origin](DCO) 1.1. Every commit must carry a
sign-off line asserting that you wrote the patch or otherwise have the right to submit it under the
Apache License 2.0:

```
Signed-off-by: Your Name <your.email@example.com>
```

`git commit -s` adds it for you. The name must be your real name, and the sign-off must match the
commit author.

## Building and testing

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

The tests have no dependencies — the harness is `tests/check.hpp`, about sixty lines. Most of the
suite is `static_assert`: if a test file compiles, the compile-time behaviour it asserts is correct.

Please also check the backends that your change can affect:

```bash
cmake -S . -B build-portable -DTESSERA_DEDUP_BACKEND=PORTABLE && cmake --build build-portable
cmake -S . -B build-fold     -DTESSERA_DEDUP_BACKEND=FOLD     && cmake --build build-fold
# and, on Clang 22 or newer:
cmake -S . -B build-builtin  -DTESSERA_DEDUP_BACKEND=BUILTIN  && cmake --build build-builtin
```

All backends must produce *identical types*, not merely equivalent behaviour. That invariant is
checked mechanically: `tests/dedup_backends.test.cpp` instantiates every implementation available on
the current toolchain — including the Clang builtin and, where the compiler has P2996, the
reflection one — on the same input and compares each against the reference fold. Adding a backend
means adding one `static_assert` there.

## What a patch should include

* A test. New behaviour gets a `static_assert` for the compile-time part and a `TESSERA_TEST` for
  anything observable at run time.
* A benchmark note, if the change touches the type algebra or the deduplication: run
  `python3 benchmarks/run_compile_bench.py` before and after and put the numbers in the pull
  request. Compile time is this library's performance surface.
* Documentation, if the change is visible to users: `docs/reference.md` for API, `docs/design.md`
  for anything about *how* it works.

## Style

* C++23, four-space indent, 120 columns; `.clang-format` in the repository root is authoritative
  (`clang-format -i include/tessera/*.hpp`).
* Types and concepts `snake_case` as in the standard library; template parameters `PascalCase`.
* Doxygen comments on everything public. Comments explain *why*; the code already says what.
* No dependencies. Not on Boost, not on a test framework, not on the compiler you happen to use:
  anything vendor-specific goes behind a probe in `config.hpp` and needs a portable fallback.
* Commit messages: an imperative subject under 72 characters, a body explaining the reasoning when
  it is not obvious, and the sign-off.
