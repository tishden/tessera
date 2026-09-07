// Copyright 2026 Denis Tishkov
// SPDX-License-Identifier: Apache-2.0

#ifndef TESSERA_GRAPH_HPP
#define TESSERA_GRAPH_HPP

#include <cstddef>
#include <utility>

#include "tessera/config.hpp"
#include "tessera/mosaic.hpp"
#include "tessera/type_list.hpp"

/// @file graph.hpp
/// @brief `tessera::resolve` — the transitive closure of a dependency graph, ordered so that every
///        dependency precedes the things that need it.
///
/// `tessera::of<...>` flattens and deduplicates a list that is already complete: whoever writes it
/// has to name every service. That is enough when components declare a flat set of leaves, and it
/// is not enough for dependency injection, where a service has services of its own and the person
/// asking for a `Router` should not have to know that it needs a `ConnectionPool` which needs a
/// `Config`.
///
/// `resolve` walks the graph instead:
///
/// @code
/// struct Config        {};
/// struct Pool          { using dependencies = tessera::type_list<Config>; };
/// struct Router        { using dependencies = tessera::type_list<Pool, Config>; };
///
/// using Services = tessera::resolve<Router>;
/// static_assert(std::same_as<Services, tessera::mosaic<Config, Pool, Router>>);
/// @endcode
///
/// Two properties come out of that, and both are asserted in `tests/graph.test.cpp`:
///
///  * **the closure is complete** — naming `Router` is enough, `Pool` and `Config` arrive by
///    themselves, each exactly once however many paths reach it;
///  * **the order is topological** — every element's dependencies sit at lower indices, so
///    initialising the mosaic front to back initialises each service after everything it needs.
///    `tessera::is_topologically_sorted` states the property directly, for asserting it on a
///    hand-written list.
///
/// The traversal is an ordinary depth-first search with the result appended in post-order, so the
/// order is a function of the declarations alone: same declarations, same order, regardless of
/// include order or which root was asked for first. A cycle is a compile error naming the type it
/// was found at; `tessera::has_dependency_cycle` reports it as a `bool` for code that wants to ask
/// rather than fail.

namespace tessera {

/// @brief What a type depends on — the customization point the traversal reads.
///
/// The primary template answers "nothing", so a leaf service needs no opt-in. A type with a
/// `dependencies` member list is picked up automatically; anything else can be taught by
/// specializing this:
///
/// @code
/// template<> struct tessera::dependencies_of<ThirdPartyThing> {
///     using type = tessera::type_list<Config>;
/// };
/// @endcode
template<class T>
struct dependencies_of {
    using type = type_list<>;
};

template<class T>
    requires requires { typename T::dependencies; }
struct dependencies_of<T> {
    using type = typename T::dependencies;
};

template<class T>
using dependencies_of_t = typename dependencies_of<T>::type;

namespace detail {

/// The state threaded through the walk: what has been emitted so far, and whether a cycle was seen.
template<class Seen, bool Cycle>
struct walk_state {
    using seen = Seen;
    static constexpr bool cycle = Cycle;
};

/// Which of the three things happens at a node. Splitting it out keeps the descending branch —
/// the only one that recurses — out of the specializations that must not recurse: a member alias
/// is instantiated with its class, so a single class holding all three answers would walk the whole
/// graph even at a node it had already finished with, and would not terminate on a cycle.
enum class walk_kind {
    finished,  ///< already emitted; nothing to do
    cyclic,    ///< currently on the path from the root: a back edge, i.e. a cycle
    descend,   ///< unvisited: emit its dependencies first, then it
};

/// Which of the three cases @p T falls into, given what has been emitted and what is on the path.
template<class Seen, class Path, class T>
inline constexpr walk_kind walk_kind_of = Seen::template contains<T>   ? walk_kind::finished
                                          : Path::template contains<T> ? walk_kind::cyclic
                                                                       : walk_kind::descend;

template<walk_kind Kind, class State, class Path, class T>
struct walk_node;

template<class State, class Path, class L>
struct walk_list;

template<class State, class Path>
struct walk_list<State, Path, type_list<>> {
    using type = State;
};

template<class State, class Path, class Head, class... Tail>
struct walk_list<State, Path, type_list<Head, Tail...>>
    : walk_list<typename walk_node<walk_kind_of<typename State::seen, Path, Head>, State, Path, Head>::type, Path,
                type_list<Tail...>> {};

template<class State, class Path, class T>
struct walk_node<walk_kind::finished, State, Path, T> {
    using type = State;
};

template<class State, class Path, class T>
struct walk_node<walk_kind::cyclic, State, Path, T> {
    // Recording the cycle rather than asserting here keeps the walk finite and lets the diagnostic
    // be raised once, at the top, where the whole root list can be named.
    using type = walk_state<typename State::seen, true>;
};

template<class State, class Path, class T>
struct walk_node<walk_kind::descend, State, Path, T> {
private:
    /// Dependencies first. `elements_of_t` is what splices a nested list, so a `dependencies` entry
    /// may itself be a `type_list` used for grouping.
    using after = typename walk_list<State, typename Path::template append<T>,
                                     elements_of_t<dependencies_of_t<T>>>::type;

public:
    /// Post-order: `T` is emitted once everything it needs already is.
    using type = walk_state<typename after::seen::template append<T>, after::cycle>;
};

template<class... Roots>
using walk_from = typename walk_list<walk_state<type_list<>, false>, type_list<>, type_list<Roots...>>::type;

/// Whether every dependency of the element at @p Position sits at a lower index. A dependency that
/// is absent altogether answers `npos`, which is not less than anything — so this checks closure
/// and order in one expression.
template<class L, std::size_t Position, class Deps>
struct dependencies_precede;

template<class L, std::size_t Position, class... Ds>
struct dependencies_precede<L, Position, type_list<Ds...>> {
    static constexpr bool value = ((L::template index_of<Ds> < Position) && ...);
};

template<class L, class Sequence>
struct sorted_check;

template<class... Ts, std::size_t... Is>
struct sorted_check<type_list<Ts...>, std::index_sequence<Is...>> {
    static constexpr bool value =
        (dependencies_precede<type_list<Ts...>, Is, elements_of_t<dependencies_of_t<Ts>>>::value && ...);
};

template<class... Roots>
struct resolved {
    static_assert(!walk_from<Roots...>::cycle,
                  "tessera::resolve — the dependency graph has a cycle; see tessera::has_dependency_cycle");
    using type = typename walk_from<Roots...>::seen;
};

}  // namespace detail

/// @brief Whether the graph reachable from @p Roots contains a cycle.
///
/// `resolve` refuses to build one, so this exists for the code that would rather ask than fail —
/// tests, and `static_assert`s with a message of their own.
template<class... Roots>
inline constexpr bool has_dependency_cycle = detail::walk_from<Roots...>::cycle;

/// @brief Whether every element of @p L is preceded by everything it depends on, and the list is
///        closed under `dependencies_of`.
///
/// Accepts a `type_list` or a `mosaic` — anything `elements_of` understands. This is the property
/// `resolve` establishes; asserting it on a hand-written list is a way to keep one honest.
template<class L>
inline constexpr bool is_topologically_sorted =
    detail::sorted_check<elements_of_t<L>, std::make_index_sequence<elements_of_t<L>::size>>::value;

/// @brief The transitive closure of @p Roots as a `type_list`, dependencies before dependents.
///
/// The roots are part of the result: `resolve` answers "everything needed to stand these up",
/// which is what a container is asked for.
template<class... Roots>
using resolved_t = typename detail::resolved<Roots...>::type;

/// @brief The transitive closure of @p Roots as a `mosaic`, dependencies before dependents.
///
/// @code
/// using Services = tessera::resolve<Router>;
/// Services services;
/// services.for_each([](auto& service) { service.start(); });   // never before its dependencies
/// @endcode
template<class... Roots>
using resolve = typename resolved_t<Roots...>::template into<mosaic>;

}  // namespace tessera

#endif  // TESSERA_GRAPH_HPP
