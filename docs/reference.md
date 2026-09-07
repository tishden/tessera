# API reference

Everything lives in namespace `tessera`; `tessera::detail` is implementation and carries no
stability promise. One umbrella header pulls in the library:

```cpp
#include <tessera/tessera.hpp>
```

| Header | Provides |
|---|---|
| `tessera/type_list.hpp` | `type_list`, the type algebra, `flatten_t`, `unique_t`, `concat_t`, `elements_of` |
| `tessera/mosaic.hpp` | `mosaic`, `of`, `mapped_of`, `broadcast` |
| `tessera/graph.hpp` | `resolve`, `resolved_t`, `dependencies_of`, `has_dependency_cycle`, `is_topologically_sorted` |
| `tessera/value_list.hpp` | `value_list`, `to_value_list_t` |
| `tessera/reflect.hpp` | `type_name`, the static-reflection seam |
| `tessera/algebra.hpp` | the two implementations of the algebra and their selection, `algebra_backend_name` |
| `tessera/config.hpp` | feature detection and configuration macros |

---

## `tessera::type_list<Ts...>`

An ordered list of types. Never instantiated as an object; duplicates are allowed.

| Member | Kind | Meaning |
|---|---|---|
| `size` | `std::size_t` | number of elements |
| `empty` | `bool` | `size == 0` |
| `contains<T>` | `bool` | whether `T` occurs in the list |
| `index_of<T>` | `std::size_t` | position of the first `T`, or `tessera::npos` |
| `nth<I>` | type | the `I`-th element |
| `into<Target>` | type | `Target<Ts...>` |
| `append<Us...>` | type | `type_list<Ts..., Us...>`, no deduplication |
| `concat<Other>` | type | concatenation with another `type_list` |
| `filter<Predicate>` | type | elements with `Predicate<T>::value`, order preserved |
| `transform<Function>` | type | `Function<T>` per element, deduplicated |
| `flat_map<Function>` | type | `Function<T>` per element with list-like results spliced in, deduplicated |
| `tessera_elements` | type | the splice hook read by `elements_of` |

Deduplication and flattening are **free** aliases rather than members, so that naming a list never
triggers them (see [design.md §3](design.md)):

| Alias | Meaning |
|---|---|
| `unique_t<L>` | `L` with duplicates removed, first occurrence winning |
| `flatten_t<Ts...>` | the flat, distinct union of `Ts` — leaves kept, list-likes spliced in |
| `concat_t<Ls...>` | concatenation of any number of lists |
| `elements_of<T>` / `elements_of_t<T>` | customization point: how `T` contributes to a flattened list |
| `npos` | sentinel returned by `index_of` |

```cpp
using L = tessera::type_list<int, double, int>;
static_assert(L::size == 3 && tessera::unique_t<L>::size == 2);
static_assert(std::same_as<L::filter<std::is_integral>, tessera::type_list<int, int>>);
```

Teaching the library about a foreign list type:

```cpp
template<class... Ts>
struct tessera::elements_of<std::tuple<Ts...>> { using type = tessera::type_list<Ts...>; };
```

---

## `tessera::mosaic<Ts...>`

Storage for exactly one value per element type. Element types must be distinct — use `of<...>` to
build one from a list that may not be.

### Construction

| Expression | Effect |
|---|---|
| `mosaic<Ts...> m;` | every element value-initialized (requires default-initializable elements) |
| `mosaic<Ts...> m{a, b};` | arguments matched to elements **by type, in any order**; elements with no matching argument are value-initialized |
| `mosaic<Ts...> m{broadcast(x)};` | every element constructed from the same value `x`; the tag holds a reference, so pass it directly into the constructor |

### Access and iteration

| Member | Meaning |
|---|---|
| `get<T>()` | the element of type `T`; const-qualification and value category follow `*this` |
| `set(value)` | assigns to the element of type `std::remove_cvref_t<decltype(value)>` |
| `emplace<T>(args...)` | replaces the element of type `T` with `T(args...)`; assigns rather than rebuilds, so `T` must be move-assignable |
| `for_each(handler)` | invokes `handler(element)` per element, in declaration order |
| `for_each_type(handler)` | static; invokes `handler.template operator()<T>()` or `handler(std::type_identity<T>{})` |
| `apply(handler)` | invokes `handler(elements...)` once, `std::apply` style |
| `operator==` | defaulted, element by element |

### Compile-time interface

| Member | Meaning |
|---|---|
| `types` | the element types as a `type_list` |
| `size`, `empty`, `contains<T>` | as on `type_list` |
| `into<Target>` | `Target<Ts...>` |
| `add<Us...>` | this mosaic plus `Us`, flattened and deduplicated |
| `filter<Predicate>` | the elements satisfying the predicate, as a mosaic |
| `transform<Function>` | `Function<T>` per element, as a mosaic |
| `flat_map<Function>` | `Function<T>` per element with list-likes spliced in, as a mosaic |
| `values<E, Extractor>` | collects `Extractor<T>::value` from every element into a `value_list<E, ...>`, deduplicated |

### Builders

| Alias | Meaning |
|---|---|
| `of<Ts...>` | a mosaic of the flat, distinct union of `Ts` — the normal way to build one |
| `mapped_of<Function, Ts...>` | `of<Function<Ts>...>` |
| `broadcast(value)` | constructor tag: build every element from `value` |

```cpp
using Engine = tessera::of<Clock, FrameBuffer, tessera::of<Clock, AssetCache>>;
static_assert(std::same_as<Engine, tessera::mosaic<Clock, FrameBuffer, AssetCache>>);

Engine engine{Clock{}, AssetCache{.loadedMeshes = 12}};   // FrameBuffer value-initialized
engine.get<FrameBuffer>().width = 1920;
engine.for_each([](auto& service) { service.reset(); });
```

---

## `tessera/graph.hpp`

`of<...>` flattens a list that is already complete. `resolve<...>` follows each type's declared
dependencies transitively and orders the result so that every element is preceded by the elements it
depends on — the two properties a dependency-injection container is asked for.

| Entity | Meaning |
|---|---|
| `dependencies_of<T>` | customization point: `::type` is a `type_list` of `T`'s **direct** dependencies. Defaults to `T::dependencies` when that member exists, otherwise to `type_list<>` |
| `dependencies_of_t<T>` | `typename dependencies_of<T>::type` |
| `resolved_t<Roots...>` | the transitive closure of `Roots` as a `type_list`, dependencies before dependents; the roots are included |
| `resolve<Roots...>` | the same closure as a `mosaic` |
| `has_dependency_cycle<Roots...>` | `constexpr bool` — whether the reachable graph has a cycle |
| `is_topologically_sorted<L>` | `constexpr bool` — whether every element of `L` is preceded by everything it depends on, and `L` is closed under `dependencies_of`. Accepts a `type_list` or a `mosaic` |

```cpp
struct Config {};
struct Pool   { using dependencies = tessera::type_list<Config>; };
struct Router { using dependencies = tessera::type_list<Pool, Config>; };

using Services = tessera::resolve<Router>;                       // one root is enough
static_assert(std::same_as<Services, tessera::mosaic<Config, Pool, Router>>);
static_assert(tessera::is_topologically_sorted<Services>);

Services services;
services.for_each([](auto& s) { s.start(); });                   // never before its dependencies
```

Teaching the traversal about a type you do not own:

```cpp
template<> struct tessera::dependencies_of<ThirdPartyThing> {
    using type = tessera::type_list<Config>;
};
```

**Order.** Depth-first, emitting each node after its dependencies, roots in the order given. The
result is a function of the declarations alone — the same graph produces the same order regardless
of include order or which root was named first — so it can be pinned with `static_assert`.

**Cycles.** `resolve` and `resolved_t` reject a cyclic graph with a `static_assert`.
`has_dependency_cycle` answers the same question as a `bool` for code that would rather test than
fail. A node reached twice by different paths is not a cycle; only a back edge to a node still on
the current path is.

**Cost.** The traversal folds over lists recursively, so it spends one instantiation level per
element of every list on the current stack — the chain being walked, the dependency list being
iterated, and the root list. Against Clang's default `-ftemplate-depth=1024` that is 510 links in a
chain, ~1010 direct dependencies of one service, or 512 roots; keep the graph shallow and the root
list short and the limit is nowhere near. Its price relative to `of<...>`, and the ceilings, are in
[benchmarks.md §1d](benchmarks.md).

---

## `tessera::value_list<T, Vs...>`

A list of constants of one type.

| Member | Meaning |
|---|---|
| `value_type` | `T` |
| `size`, `empty` | as elsewhere |
| `contains(value)` | whether `value` is one of the constants (constexpr) |
| `for_each(handler)` | invokes the handler once per constant |
| `visit_until(handler)` | invokes handlers until one returns `true`; returns whether any did |
| `dispatch(value, handler)` | invokes the handler with the constant equal to `value`, at most once; returns whether one matched |
| `into<Target>` | `Target<Vs...>` |
| `into_types<Target, Box>` | `Target<Box<Vs>...>` |
| `as_type_list` | the constants as `std::integral_constant` types |

Handlers may be written either way: `[]<Format F>() { … }` or
`[](auto constant) { … decltype(constant)::value … }`.

| Alias | Meaning |
|---|---|
| `to_value_list_t<T, L>` | builds a `value_list` from a list of `std::integral_constant`-like types |

```cpp
using SupportedFormats = LinkedDecoders::values<Format, format_of>;
SupportedFormats::dispatch(file.format, [&]<Format F>() { Decoder<F>::decode(file.byteCount); });
```

---

## `tessera/reflect.hpp`

| Entity | Meaning |
|---|---|
| `type_name<T>()` | consteval, a human-readable name for `T` (reflection where available, otherwise the compiler's pretty function name) |
| `members_of_t<Aggregate>` | the member types of an aggregate as a `type_list` — **requires P2996**, see [reflection.md](reflection.md) |

---

## Configuration macros

Defined in `config.hpp`; only `TESSERA_ALGEBRA_BACKEND` is meant to be set by a build.

| Macro | Meaning |
|---|---|
| `TESSERA_VERSION_MAJOR/MINOR/PATCH`, `TESSERA_VERSION_STRING` | library version |
| `TESSERA_ALGEBRA_BACKEND` | `TESSERA_ALGEBRA_PORTABLE` \| `_BUILTIN` \| `_REFLECTION` \| `_FOLD`; auto-selected if unset |
| `TESSERA_ALGEBRA_BACKEND_NAME` | the selected backend as a string; also `tessera::algebra_backend_name` |
| `TESSERA_HAS_BUILTIN_DEDUP_PACK` | Clang 22+ `__builtin_dedup_pack` available |
| `TESSERA_HAS_TYPE_PACK_ELEMENT` | Clang `__type_pack_element` available |
| `TESSERA_HAS_REFLECTION` | P2996 static reflection available |
| `TESSERA_ENABLE_REFLECTION_BACKEND` | opt in to the experimental reflection backend |
| `TESSERA_REFLECTION_HEADER` | the reflection header that was found: `<meta>` or `<experimental/meta>` |

Both implementations are always reachable: `tessera::detail::tmpl::*` and, where the toolchain has
reflection, `tessera::detail::refl::*`. Every one the compiler can build *is* built, whether or not
it is selected, so that `tests/algebra_backends.test.cpp` can compare them operation by operation.
`tessera::detail::ops` names the selected one.

From CMake:

```cmake
set(TESSERA_ALGEBRA_BACKEND PORTABLE)   # AUTO | PORTABLE | BUILTIN | REFLECTION | FOLD
add_subdirectory(external/tessera)
target_link_libraries(my_app PRIVATE tessera::tessera)
```

For the reflection backend there is a toolchain file — `cmake/toolchains/clang-p2996.cmake` — that
configures the P2996 fork of Clang correctly; see [reflection.md](reflection.md).
