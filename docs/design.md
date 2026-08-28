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

## 4. One algebra, two implementations

Everything the public types do with lists goes through five operations, and nothing else:

| operation | meaning |
|---|---|
| `unique_into<Target, Ts...>` | drop duplicates, feed the survivors to `Target` |
| `splice_unique_into<Target, Ls...>` | splice the lists' elements, drop duplicates, feed `Target` |
| `concat_into<Target, Ls...>` | concatenate, keep duplicates, feed `Target` |
| `select_into<Target, Mask, Ts...>` | keep the elements whose mask bit is set |
| `nth<I, Ts...>` | the I-th element |

Two things about the shape of that list matter.

**They take the target template.** `tessera::of<...>` produces a `mosaic` in one step instead of
building a `type_list` that is immediately converted; `mosaic::flat_map` likewise. Every fused
operation is one class specialization the compiler does not have to create, name and keep.

**They are the whole seam.** `detail::tmpl` implements them with template metaprogramming,
`detail::refl` with static reflection, and `algebra.hpp` picks one with a namespace alias. Neither
`type_list` nor `mosaic` mentions either implementation.

### The template implementation

**Deduplication** is the expensive part. `TESSERA_ALGEBRA_FOLD` is the obvious way — append a type
if the accumulator does not hold it. Four lines, the fastest thing there is for short lists, one
instantiation level per element, and therefore dead at a few hundred components: it runs past the
compiler's 1024-deep limit. `TESSERA_ALGEBRA_PORTABLE`, the default, is a hybrid: lists up to
`fold_threshold` (256) go to the fold, longer ones are halved, deduplicated and merged.

The merge is where the second trick lives. Testing membership with `is_same` would cost `|A| × |B|`
instantiations, so the left half — already unique — becomes a class that inherits one tag per
element:

```cpp
template<class T> struct type_tag {};
template<class... Ts> struct type_set : type_tag<Ts>... {};

template<class T, class Set>
inline constexpr bool is_member_of = std::is_base_of_v<type_tag<T>, Set>;
```

Membership is then a base-class lookup, answered from the table the compiler built once. The
construction is only well-formed because the left half is unique — repeating a base class is
ill-formed — which is exactly the invariant the algorithm maintains.

`TESSERA_ALGEBRA_BUILTIN` replaces the deduplication with `__builtin_dedup_pack<Ts...>` where Clang
22 provides it: one expansion, no library recursion.

**Concatenation** is the other hot spot, because filter, flat_map and the merge all funnel through
it, joining a list per element. Folded two lists at a time it costs one intermediate specialization
per input list, each one element longer than the last. Instead a `consteval` loop computes, for
every element of the result, which input list it comes from and at which position, and one pack
expansion over that plan produces the answer: one instantiation, constant depth, no accumulator.

### The reflection implementation

`detail::refl` computes with values rather than types. Each operation is one `consteval` function
that builds a `std::vector<std::meta::info>`, does the set algebra with an ordinary loop, and calls
`std::meta::substitute` once:

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
```

Nothing intermediate is a type, so nothing intermediate is retained. The measured effect is in
[benchmarks.md](benchmarks.md); the constraints that shape the code — a splice operand may not still
own a vector, feature probes may not call `consteval` functions — are in
[reflection.md](reflection.md).

Note what does *not* move: a predicate like `Predicate<T>::value` is the user's template, and the
compiler instantiates it once per element either way. Only the list surgery changes hands, which is
why `filter` passes a bit mask into the algebra rather than the predicate itself.

## 5. Keeping vendor extensions contained

Two Clang builtins are used when available — `__builtin_dedup_pack` and `__type_pack_element` — and
both are detected in `config.hpp` behind `defined(__clang__) && __has_builtin(...)`, exposed as
`TESSERA_HAS_*` macros. No other header mentions a compiler. Both have portable fallbacks: the
hybrid deduplication above, and pack indexing through a tagged-base indexer that resolves an index
to a type by overload resolution rather than by walking the pack.

## 6. What it costs the compiler

Numbers are in [benchmarks.md](benchmarks.md); the shape of them is:

* a mosaic of N components costs roughly what a `std::tuple` of N components costs — the container
  is not where the time goes;
* assembling that mosaic out of N components' declarations (4N type mentions) roughly doubles it on
  the portable backend and adds about a quarter on the builtin one;
* everything grows superlinearly, so the practical range is components in the tens to low hundreds
  per translation unit — which is the range this design is for. Beyond that the answer is not a
  faster metafunction, it is fewer types per translation unit.

## 7. Deliberately left out

* **Index-based access as the primary API.** `nth<I>` exists for the algebra; the container is
  addressed by type on purpose.
* **Runtime insertion or removal.** The element set is a type; changing it produces a different
  type.
* **A general tuple interop layer.** `elements_of` is a customization point — specializing it for
  `std::tuple` is three lines — but the library does not decide for the user that a tuple should
  splice itself into an assembly.
* **Exception guarantees beyond the elements'.** A mosaic is as exception-safe as the types it
  holds; it adds no state of its own.
