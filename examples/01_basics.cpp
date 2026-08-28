// Copyright 2026 Denis Tishkov
// SPDX-License-Identifier: Apache-2.0
//
// Example 1 — the container itself: build it, address it by type, walk it.
//
//     cmake --build build --target tessera_example_01_basics && ./build/examples/01_basics

#include <cstdio>
#include <string>
#include <type_traits>

#include <tessera/tessera.hpp>

namespace {

struct Timeout {
    int milliseconds = 0;
};

// A stateless policy: it participates in the type list but occupies no storage.
struct FailFastPolicy {};

}  // namespace

int main() {
    // `of<...>` accepts duplicates and nested lists; the resulting mosaic holds each type once.
    using Session = tessera::of<int, std::string, Timeout, tessera::type_list<int, FailFastPolicy>>;
    static_assert(std::is_same_v<Session, tessera::mosaic<int, std::string, Timeout, FailFastPolicy>>);
    static_assert(Session::size == 4);
    static_assert(Session::contains<Timeout>);

    // Arguments are matched to elements by type, in any order; the rest are value-initialized.
    Session session{Timeout{250}, std::string{"cache-warmup"}};
    session.set(42);

    std::printf("name     : %s\n", session.get<std::string>().c_str());
    std::printf("timeout  : %d ms\n", session.get<Timeout>().milliseconds);
    std::printf("counter  : %d\n", session.get<int>());

    // Iteration over values — the handler is instantiated once per element type, so there is no
    // type erasure and nothing virtual involved.
    int elements = 0;
    session.for_each([&](const auto&) { ++elements; });
    std::printf("elements : %d\n", elements);

    // Iteration over types — no object needed.
    std::printf("types    :");
    Session::for_each_type([]<class T>() {
        const std::string_view name = tessera::type_name<T>();
        std::printf(" %.*s", static_cast<int>(name.size()), name.data());
    });
    std::printf("\n");

    // The stateless policy costs nothing: the mosaic is as big as its stateful elements.
    std::printf("sizeof   : %zu (int + std::string + Timeout + a stateless policy)\n", sizeof(Session));
    std::printf("backend  : %s\n", tessera::dedup_backend_name);

    return 0;
}
