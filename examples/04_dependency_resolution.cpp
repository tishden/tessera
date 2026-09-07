// Copyright 2026 Denis Tishkov
// SPDX-License-Identifier: Apache-2.0
//
// Example 4 — dependency injection with nothing left to resolve at run time.
//
// Each service declares only what it directly needs. Nobody writes the full list, nobody writes the
// start-up order, and neither exists as data in the binary: `tessera::resolve` computes the
// transitive closure while the program is being compiled and orders it so that every service is
// preceded by the services it depends on. Walking the container front to back is therefore a valid
// start-up order, and walking it backwards is a valid shutdown order.
//
//     cmake --build build --target tessera_example_04_dependency_resolution
//     ./build/examples/04_dependency_resolution

#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>

#include <tessera/tessera.hpp>

namespace {

// ---- the services ------------------------------------------------------------------------------
//
// `dependencies` names only the direct edges. Nothing here mentions the graph as a whole.

struct Config {
    std::string dsn = "postgres://localhost/app";
};

struct Log {
    std::vector<std::string> lines;

    void write(std::string line) {
        lines.push_back(std::move(line));
    }
};

struct ConnectionPool {
    using dependencies = tessera::type_list<Config, Log>;
    int connections = 0;
};

struct MigrationRunner {
    using dependencies = tessera::type_list<ConnectionPool, Log>;
    int applied = 0;
};

struct UserRepository {
    using dependencies = tessera::type_list<ConnectionPool>;
};

struct HttpRouter {
    using dependencies = tessera::type_list<UserRepository, MigrationRunner, Log>;
    int routes = 0;
};

// ---- assembly ----------------------------------------------------------------------------------

// One root. Everything below it arrives on its own, exactly once however many paths reach it —
// `ConnectionPool` is named by two services and `Log` by three.
using Services = tessera::resolve<HttpRouter>;

static_assert(std::is_same_v<Services, tessera::mosaic<Config, Log, ConnectionPool, UserRepository,
                                                       MigrationRunner, HttpRouter>>,
              "the resolved set and its order are fixed at compile time and can be asserted on");

// The property the order is chosen for, stated directly rather than inferred from the list above:
// every element is preceded by everything it depends on.
static_assert(tessera::is_topologically_sorted<Services>);

// And the graph is acyclic — `resolve` would refuse to build it otherwise.
static_assert(!tessera::has_dependency_cycle<HttpRouter>);

// `of<...>` is the other builder, and the contrast is the point of this example: it flattens and
// deduplicates the list it is given, and follows no edges.
static_assert(tessera::of<HttpRouter>::size == 1);

// ---- start-up ----------------------------------------------------------------------------------

/// Started by walking the container in order. A real service would open sockets here; this one just
/// records that its turn came, so that the ordering is visible in the output.
template<class Service>
void start(Service& service, Log& log) {
    log.write(std::string{"started "} + std::string{tessera::type_name<Service>()});
    if constexpr (std::is_same_v<Service, ConnectionPool>) {
        service.connections = 8;
    } else if constexpr (std::is_same_v<Service, MigrationRunner>) {
        service.applied = 3;
    } else if constexpr (std::is_same_v<Service, HttpRouter>) {
        service.routes = 12;
    }
}

}  // namespace

int main() {
    Services services;

    std::printf("resolved %zu services from one root:\n\n", Services::size);

    Log& log = services.get<Log>();
    services.for_each([&](auto& service) { start(service, log); });

    for (const std::string& line : log.lines) {
        std::printf("  %s\n", line.c_str());
    }

    std::printf("\npool connections : %d\n", services.get<ConnectionPool>().connections);
    std::printf("migrations       : %d\n", services.get<MigrationRunner>().applied);
    std::printf("routes           : %d\n", services.get<HttpRouter>().routes);
    std::printf("backend          : %s\n", tessera::algebra_backend_name);

    return 0;
}
