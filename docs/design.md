# Design notes

How Tessera is built, and why it is built that way. The public shape of the library is small —
three types and a handful of aliases — so most of what is interesting is underneath.

## 1. Addressed by type

A `std::tuple` is addressed by position, which makes it a poor fit for a set of components: the
positions are a second source of truth that has to be kept in sync with the declaration, and any
insertion renumbers everything after it. `std::get<T>` fixes the spelling but not the model — the
tuple still has to be written out by hand, duplicates and all.

Tessera makes the *set of types* the identity of the container. `mosaic<Ts...>` holds one value per
type, `get<T>()` reaches it, and two mosaics with the same element types are the same type
regardless of how they were assembled. That is what lets `of<...>` accept a redundant, nested,
independently contributed list of types and still produce something that can be `static_assert`ed
against a hand-written expectation.

The cost of the model is the constraint that follows from it: a type occurs at most once. A system
that needs two frame buffers needs two distinct types (`FrameBuffer<Colour>`, `FrameBuffer<Depth>`),
which in practice is what a typed design wants anyway.

## 2. Storage

```cpp
template<class T>
struct slot { [[no_unique_address]] T value; };

template<class... Ts>
class mosaic : private detail::slot<Ts>... { … };
```

One private base per element. The layout is the layout of a plain struct with those members, and
`[[no_unique_address]]` collapses stateless elements to nothing — a mosaic of three empty policies
is one byte, the same as `std::tuple`. Nothing here is virtual, nothing is allocated, and the
container is trivially copyable when its elements are.

`get<T>` is a single function using an explicit object parameter (C++23), which is why the library
requires C++23 rather than C++20:

```cpp
template<class T, class Self>
constexpr auto&& get(this Self&& self) noexcept;
```

One definition covers `&`, `const&`, `&&` and `const&&` and propagates the caller's constness and
value category to the returned reference. Written with overloads it would be four near-identical
bodies — and the same is true of `for_each` and `apply`.

Base lookup deserves a note: the elements are *private* bases, so the derived-to-base conversion is
only accessible inside the class. `get` performs it through a helper that deduces `T` from
`slot<T>&`, which keeps the member-access syntax free of dependent-name qualification while leaving
`slot` invisible to users.

## 3. The algebra, and one rule about laziness

Everything in `type_list` is a metafunction returning a type; nothing is a runtime value. The rule
that matters when writing them:

> A **non-template** member alias is instantiated together with its class. A member **template** is
> instantiated only when it is named.

The first version of `type_list` had `using unique = …;` and `using flatten = …;` as ordinary
member aliases. Every mention of any list — including the intermediate lists created *inside* the
deduplication algorithm — therefore deduplicated itself, eagerly, whether or not anyone asked. Aside
from the wasted work, it is circular: computing `unique` needs `L::size`, which needs the class to
be complete, which is what is currently being instantiated.

So deduplication and flattening are free aliases (`unique_t<L>`, `flatten_t<Ts...>`), and every
member that does real work (`filter`, `transform`, `flat_map`, `contains`, `nth`) is a template.
The rest of the members are constants and one-line aliases that cost nothing.

## 4. Concatenation without an accumulator

`filter`, `flat_map` and the merge step all end up joining a list per element. Written the textbook
way — fold two lists at a time — each input list adds one intermediate specialization, each one
element longer than the last: quadratic in retained types, linear in instantiation depth.

`detail::concat` instead computes a *plan* in a consteval loop — for every element of the result,
which input list it comes from and at which position — and produces the result in a single pack
expansion over that plan. One instantiation, constant depth, no accumulator.

## 5. Deduplication: four backends and a hybrid

Deduplication is the expensive part of assembly, so it is isolated in `dedup.hpp` behind one alias
(`detail::deduplicated_t`) with four implementations. All of them produce the identical type: the
first occurrence of each type survives, in its original position.

**FOLD** is the obvious one — append a type if the accumulator does not hold it. Four lines,
and the fastest thing there is for short lists. It also has one instantiation level per element, so
it hits the compiler's 1024-deep limit at a few hundred components, and it retains one list per
step.

**PORTABLE**, the default, is a hybrid. Lists up to `fold_threshold` (256) elements go straight to
the fold, which is what keeps the constant factor low; longer lists are halved, each half
deduplicated, and the results merged. Depth is bounded by the threshold plus a logarithmic number of
merge levels, so nothing on the way to a few thousand types comes near the limit.

The merge step is where the second trick lives. Testing whether a candidate occurs in the left half
with `is_same` costs one instantiation per element, so a merge would cost `|A| × |B|`. Instead the
left half — already unique — is turned into a class that inherits one tag per element:

```cpp
template<class T> struct type_tag {};
template<class... Ts> struct type_set : type_tag<Ts>... {};

template<class T, class Set>
inline constexpr bool is_member_of = std::is_base_of_v<type_tag<T>, Set>;
```

Membership is then a base-class lookup, which the compiler answers from the table it built once for
the set. The construction is only well-formed because the left half is unique — repeating a base
class is ill-formed — which is exactly the invariant the algorithm maintains.

**BUILTIN** replaces all of it with `__builtin_dedup_pack<Ts...>` where Clang 22 provides it: one
expansion, no library recursion, and — as the benchmarks show — roughly half the compile time and a
third of the compiler memory of the portable path at 256 components.

**REFLECTION** is the P2996 sketch: `std::vector<std::meta::info>`, an ordinary loop, one
`substitute`. See [reflection.md](reflection.md).

The choice is made in `config.hpp` and nowhere else; a build system can pin it
(`-DTESSERA_DEDUP_BACKEND=PORTABLE`), which is how the benchmark measures one against another on the
same toolchain. Every backend the toolchain can compile is compiled — selection only decides which
one `deduplicated_t` forwards to — so `tests/dedup_backends.test.cpp` can instantiate all of them
side by side and assert they produce the identical type, which is the property the whole
substitution rests on.

## 6. Keeping vendor extensions contained

Two Clang builtins are used when available — `__builtin_dedup_pack` and `__type_pack_element` — and
both are detected in `config.hpp` behind `defined(__clang__) && __has_builtin(...)`, exposed as
`TESSERA_HAS_*` macros. No other header mentions a compiler. Both have portable fallbacks: the
hybrid deduplication above, and pack indexing through a tagged-base indexer that resolves an index
to a type by overload resolution rather than by walking the pack.

## 7. What it costs the compiler

Numbers are in [benchmarks.md](benchmarks.md); the shape of them is:

* a mosaic of N components costs roughly what a `std::tuple` of N components costs — the container
  is not where the time goes;
* assembling that mosaic out of N components' declarations (4N type mentions) roughly doubles it on
  the portable backend and adds about a quarter on the builtin one;
* everything grows superlinearly, so the practical range is components in the tens to low hundreds
  per translation unit — which is the range this design is for. Beyond that the answer is not a
  faster metafunction, it is fewer types per translation unit.

## 8. Deliberately left out

* **Index-based access as the primary API.** `nth<I>` exists for the algebra; the container is
  addressed by type on purpose.
* **Runtime insertion or removal.** The element set is a type; changing it produces a different
  type.
* **A general tuple interop layer.** `elements_of` is a customization point — specializing it for
  `std::tuple` is three lines — but the library does not decide for the user that a tuple should
  splice itself into an assembly.
* **Exception guarantees beyond the elements'.** A mosaic is as exception-safe as the types it
  holds; it adds no state of its own.
