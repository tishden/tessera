// Copyright 2026 Denis Tishkov
// SPDX-License-Identifier: Apache-2.0

#include <concepts>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include <tessera/graph.hpp>
#include <tessera/tessera.hpp>

#include "check.hpp"

/// @file graph.test.cpp
/// @brief `tessera::resolve` — transitive closure and topological order.
///
/// Two properties carry the whole feature, and both are asserted rather than described: the result
/// contains everything reachable from the roots exactly once, and every element is preceded by the
/// things it depends on. The second one is what makes the container safe to walk front to back, so
/// it is checked structurally (`is_topologically_sorted`) as well as on hand-written expectations —
/// an implementation that happened to emit a correct-looking order for one graph would still have
/// to satisfy the property on a generated one.

namespace {

using tessera::has_dependency_cycle;
using tessera::is_topologically_sorted;
using tessera::mosaic;
using tessera::resolve;
using tessera::resolved_t;
using tessera::type_list;

// ---- a small service graph ---------------------------------------------------------------------

struct Config {};
struct Clock {};
struct Pool {
    using dependencies = type_list<Config>;
};
struct Cache {
    using dependencies = type_list<Pool, Clock>;
};
struct Router {
    using dependencies = type_list<Cache, Config>;
};

// ---- leaves and empties -------------------------------------------------------------------------

static_assert(std::same_as<resolved_t<>, type_list<>>);
static_assert(std::same_as<resolved_t<Config>, type_list<Config>>);
static_assert(std::same_as<resolve<Config>, mosaic<Config>>);
static_assert(std::same_as<tessera::dependencies_of_t<Config>, type_list<>>,
              "a type without a `dependencies` member depends on nothing, with no opt-in");

// ---- transitivity: naming the root is enough ----------------------------------------------------

static_assert(std::same_as<resolved_t<Router>, type_list<Config, Pool, Clock, Cache, Router>>);
static_assert(resolved_t<Router>::size == 5, "Pool, Clock and Config arrive without being named");
static_assert(is_topologically_sorted<resolved_t<Router>>);

// This is the property `of<>` does *not* have, and the reason this header exists: it flattens what
// it is given and follows nothing.
static_assert(tessera::of<Router>::size == 1);

// ---- a diamond: shared dependency appears once, before both users -------------------------------

struct Leaf {};
struct Left {
    using dependencies = type_list<Leaf>;
};
struct Right {
    using dependencies = type_list<Leaf>;
};
struct Top {
    using dependencies = type_list<Left, Right>;
};

static_assert(std::same_as<resolved_t<Top>, type_list<Leaf, Left, Right, Top>>);
static_assert(is_topologically_sorted<resolved_t<Top>>);

// ---- several roots, overlapping graphs -----------------------------------------------------------

static_assert(std::same_as<resolved_t<Cache, Router>, type_list<Config, Pool, Clock, Cache, Router>>);
static_assert(std::same_as<resolved_t<Router, Cache>, type_list<Config, Pool, Clock, Cache, Router>>);
static_assert(is_topologically_sorted<resolved_t<Pool, Router, Clock>>);

// A root that is already a dependency of another root does not appear twice, whichever order the
// roots are given in.
static_assert(resolved_t<Router, Pool, Config>::size == 5);
static_assert(resolved_t<Config, Pool, Router>::size == 5);

// ---- the order is a property of the declarations, not of the include order ------------------------

static_assert(std::same_as<resolved_t<Router>, resolved_t<Router>>);
static_assert(std::same_as<resolve<Router>, resolved_t<Router>::into<mosaic>>);

// ---- `is_topologically_sorted` says no when it should --------------------------------------------

static_assert(!is_topologically_sorted<type_list<Router, Cache, Pool, Clock, Config>>,
              "reversed: every dependency comes after its user");
static_assert(!is_topologically_sorted<type_list<Pool, Config>>, "Pool precedes the Config it needs");
static_assert(!is_topologically_sorted<type_list<Pool>>, "Config is missing entirely: not closed");
static_assert(is_topologically_sorted<type_list<>>);
static_assert(is_topologically_sorted<type_list<Config, Clock>>, "independent leaves, any order");

// It accepts a mosaic as readily as a list.
static_assert(is_topologically_sorted<resolve<Router>>);

// ---- cycles ---------------------------------------------------------------------------------------

struct Odd;
struct Even {
    using dependencies = type_list<Odd>;
};
struct Odd {
    using dependencies = type_list<Even>;
};

struct SelfReferential {
    using dependencies = type_list<SelfReferential>;
};

// A cycle three edges long, reached through an acyclic prefix.
struct C3;
struct C1 {
    using dependencies = type_list<C3>;
};
struct C2 {
    using dependencies = type_list<C1>;
};
struct C3 {
    using dependencies = type_list<C2>;
};
struct Entry {
    using dependencies = type_list<Config, C1>;
};

static_assert(has_dependency_cycle<Even>);
static_assert(has_dependency_cycle<Odd>);
static_assert(has_dependency_cycle<SelfReferential>);
static_assert(has_dependency_cycle<Entry>, "a cycle behind an acyclic prefix is still found");
static_assert(has_dependency_cycle<Config, Even>, "one bad root is enough");

static_assert(!has_dependency_cycle<>);
static_assert(!has_dependency_cycle<Router>);
static_assert(!has_dependency_cycle<Top>);
static_assert(!has_dependency_cycle<Config, Clock, Pool, Cache, Router>);

// A diamond is not a cycle — the same node reached twice by different paths must not be mistaken
// for a back edge, which is exactly what a `seen`-only check would get wrong.
static_assert(!has_dependency_cycle<Top>);

// ---- the customization point --------------------------------------------------------------------

struct ThirdPartyThing {};  // not ours: cannot be given a `dependencies` member

}  // namespace

template<>
struct tessera::dependencies_of<ThirdPartyThing> {
    using type = tessera::type_list<Config>;
};

namespace {

static_assert(std::same_as<resolved_t<ThirdPartyThing>, type_list<Config, ThirdPartyThing>>);
static_assert(is_topologically_sorted<resolved_t<ThirdPartyThing>>);

// ---- a dependency entry may itself be a list, used for grouping ----------------------------------

using Storage = type_list<Config, Pool>;

struct Grouped {
    using dependencies = type_list<Storage, Clock>;
};

static_assert(std::same_as<resolved_t<Grouped>, type_list<Config, Pool, Clock, Grouped>>);
static_assert(is_topologically_sorted<resolved_t<Grouped>>);

// ---- a generated graph, so that the order cannot be right by accident ----------------------------
//
// Node<I> depends on Node<I/2> and Node<I/3>, both strictly smaller for I > 0, so the graph is a DAG
// with plenty of shared nodes and several paths to most of them.

template<std::size_t I>
struct Node {
    using dependencies = type_list<Node<I / 2>, Node<I / 3>>;
};

template<>
struct Node<0> {};

template<class Sequence>
struct all_nodes;

template<std::size_t... Is>
struct all_nodes<std::index_sequence<Is...>> {
    using roots = type_list<Node<Is>...>;
    using resolved = resolved_t<Node<Is>...>;
};

using Generated = all_nodes<std::make_index_sequence<64>>;

static_assert(Generated::resolved::size == 64, "every node reachable, each exactly once");
static_assert(is_topologically_sorted<Generated::resolved>);
static_assert(!has_dependency_cycle<Generated::roots>);

// Resolving only the deepest node still pulls in everything that node needs, and stays sorted.
static_assert(is_topologically_sorted<resolved_t<Node<63>>>);
static_assert(resolved_t<Node<63>>::size < 64, "a single root does not reach every node");

// ---- storage built from the resolution ------------------------------------------------------------

struct Started {
    std::vector<std::string>* log = nullptr;
};

}  // namespace

TESSERA_TEST(resolve_builds_a_usable_mosaic) {
    resolve<Router> services;
    CHECK_EQ(services.size, std::size_t{5});
    CHECK(services.contains<Config>);
    CHECK(services.contains<Router>);
}

TESSERA_TEST(for_each_visits_dependencies_before_their_users) {
    // The point of the ordering: walking the container front to back is a valid start-up order.
    std::vector<std::string_view> order;
    resolve<Router> services;
    services.for_each([&](auto& service) {
        using Service = std::remove_cvref_t<decltype(service)>;
        order.push_back(tessera::type_name<Service>());
    });

    const auto position = [&](std::string_view name) {
        for (std::size_t i = 0; i < order.size(); ++i) {
            if (order[i].find(name) != std::string_view::npos) {
                return static_cast<long>(i);
            }
        }
        return -1L;
    };

    CHECK_EQ(order.size(), std::size_t{5});
    CHECK(position("Config") < position("Pool"));
    CHECK(position("Pool") < position("Cache"));
    CHECK(position("Clock") < position("Cache"));
    CHECK(position("Cache") < position("Router"));
    CHECK(position("Config") < position("Router"));
}

TESSERA_TEST(resolved_mosaic_has_the_layout_of_its_sorted_list) {
    // Nothing about the ordering costs storage: it is the same elements in a chosen order.
    CHECK_EQ(sizeof(resolve<Router>), sizeof(mosaic<Config, Pool, Clock, Cache, Router>));
}
