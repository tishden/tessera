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
| `tessera/value_list.hpp` | `value_list`, `to_value_list_t` |
| `tessera/reflect.hpp` | `type_name`, the static-reflection seam |
| `tessera/dedup.hpp` | the deduplication backends, `dedup_backend_name` |
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

Defined in `config.hpp`; only `TESSERA_DEDUP_BACKEND` is meant to be set by a build.

| Macro | Meaning |
|---|---|
| `TESSERA_VERSION_MAJOR/MINOR/PATCH`, `TESSERA_VERSION_STRING` | library version |
| `TESSERA_DEDUP_BACKEND` | `TESSERA_DEDUP_PORTABLE` \| `_BUILTIN` \| `_REFLECTION` \| `_FOLD`; auto-selected if unset |
| `TESSERA_DEDUP_BACKEND_NAME` | the selected backend as a string; also `tessera::dedup_backend_name` |
| `TESSERA_HAS_BUILTIN_DEDUP_PACK` | Clang 22+ `__builtin_dedup_pack` available |
| `TESSERA_HAS_TYPE_PACK_ELEMENT` | Clang `__type_pack_element` available |
| `TESSERA_HAS_REFLECTION` | P2996 static reflection available |
| `TESSERA_ENABLE_REFLECTION_BACKEND` | opt in to the experimental reflection backend |
| `TESSERA_REFLECTION_HEADER` | the reflection header that was found: `<meta>` or `<experimental/meta>` |

Each implementation is also reachable directly as `tessera::detail::unique_fold_t`,
`unique_portable_t`, `unique_builtin_t` and `unique_reflection_t` — every one that the current
toolchain supports is compiled, whether or not it is the selected backend, so that
`tests/dedup_backends.test.cpp` can compare them against each other.

From CMake:

```cmake
set(TESSERA_DEDUP_BACKEND PORTABLE)   # AUTO | PORTABLE | BUILTIN | REFLECTION
add_subdirectory(external/tessera)
target_link_libraries(my_app PRIVATE tessera::tessera)
```
