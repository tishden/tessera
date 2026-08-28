// Copyright 2026 Denis Tishkov
// SPDX-License-Identifier: Apache-2.0
//
// Runtime side of the measurements: what the compile-time assembly buys at run time, and what a
// mosaic costs in memory compared with the obvious alternatives.
//
//     cmake --build build --target tessera_bench_runtime && ./build/benchmarks/bench_runtime

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <tessera/tessera.hpp>

namespace {

// ---- harness ---------------------------------------------------------------------------------

template<class F>
double nanosecondsPerOperation(std::size_t operations, F&& body) {
    const auto start = std::chrono::steady_clock::now();
    body();
    const auto stop = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double, std::nano>(stop - start).count();
    return elapsed / static_cast<double>(operations);
}

/// Keeps the optimizer from deleting the work being measured.
volatile std::int64_t sink = 0;

/// Optimization barrier: the value must be materialized, and memory is considered clobbered.
/// Without it a compile-time-assembled loop folds away completely and the comparison is void.
template<class T>
inline void doNotOptimize(T& value) {
#if defined(__clang__) || defined(__GNUC__)
    asm volatile("" : "+r,m"(value) : : "memory");
#else
    volatile T copy = value;
    value = copy;
#endif
}

// ---- memory ----------------------------------------------------------------------------------

struct Empty1 {};
struct Empty2 {};
struct Empty3 {};

struct Small {
    std::int32_t a = 0;
};

struct Medium {
    double a = 0;
    double b = 0;
};

void reportMemory() {
    std::printf("## Memory (sizeof, bytes)\n\n");
    std::printf("| composition                       | tessera::mosaic | std::tuple |\n");
    std::printf("|-----------------------------------|-----------------|------------|\n");
    std::printf("| 3 stateless policies              | %15zu | %10zu |\n",
                sizeof(tessera::mosaic<Empty1, Empty2, Empty3>), sizeof(std::tuple<Empty1, Empty2, Empty3>));
    std::printf("| 2 stateless + 1 small             | %15zu | %10zu |\n",
                sizeof(tessera::mosaic<Empty1, Empty2, Small>), sizeof(std::tuple<Empty1, Empty2, Small>));
    std::printf("| small + medium + string           | %15zu | %10zu |\n",
                sizeof(tessera::mosaic<Small, Medium, std::string>), sizeof(std::tuple<Small, Medium, std::string>));
    std::printf("| 8 small components                | %15zu | %10zu |\n\n",
                sizeof(tessera::mosaic<Small, Medium, Empty1, Empty2, Empty3, std::int8_t, std::int16_t, double>),
                sizeof(std::tuple<Small, Medium, Empty1, Empty2, Empty3, std::int8_t, std::int16_t, double>));
}

// ---- system dispatch: compile-time assembly vs a runtime container of interfaces ---------------

struct SystemInterface {
    virtual ~SystemInterface() = default;
    virtual void step(std::int64_t tick) = 0;
};

template<int Id>
struct System final : SystemInterface {
    std::int64_t accumulator = 0;

    void step(std::int64_t tick) override {
        accumulator += tick * Id;
    }
};

using Systems = tessera::mosaic<System<1>, System<2>, System<3>, System<4>, System<5>, System<6>, System<7>, System<8>>;

void reportSystemDispatch(std::size_t iterations) {
    const std::size_t operations = iterations * Systems::size;

    Systems mosaicSystems;
    const double mosaicNs = nanosecondsPerOperation(operations, [&] {
        for (std::size_t i = 0; i < iterations; ++i) {
            std::int64_t tick = static_cast<std::int64_t>(i);
            doNotOptimize(tick);
            mosaicSystems.for_each([&](auto& system) { system.step(tick); });
            doNotOptimize(mosaicSystems);
        }
    });
    mosaicSystems.for_each([](auto& system) { sink += system.accumulator; });

    std::vector<std::unique_ptr<SystemInterface>> virtualSystems;
    virtualSystems.push_back(std::make_unique<System<1>>());
    virtualSystems.push_back(std::make_unique<System<2>>());
    virtualSystems.push_back(std::make_unique<System<3>>());
    virtualSystems.push_back(std::make_unique<System<4>>());
    virtualSystems.push_back(std::make_unique<System<5>>());
    virtualSystems.push_back(std::make_unique<System<6>>());
    virtualSystems.push_back(std::make_unique<System<7>>());
    virtualSystems.push_back(std::make_unique<System<8>>());

    const double virtualNs = nanosecondsPerOperation(operations, [&] {
        for (std::size_t i = 0; i < iterations; ++i) {
            std::int64_t tick = static_cast<std::int64_t>(i);
            doNotOptimize(tick);
            for (auto& system : virtualSystems) {
                system->step(tick);
            }
            doNotOptimize(virtualSystems);
        }
    });
    sink += static_cast<std::int64_t>(virtualSystems.size());

    std::printf("## System dispatch (%zu calls)\n\n", operations);
    std::printf("| dispatch                          |    ns/call |\n");
    std::printf("|-----------------------------------|------------|\n");
    std::printf("| mosaic::for_each (compile-time)   | %10.3f |\n", mosaicNs);
    std::printf("| virtual through a vector          | %10.3f |\n\n", virtualNs);
}

// ---- value dispatch: value_list vs a hash map of function pointers ------------------------------

enum class Opcode : int { A = 1, B = 2, C = 3, D = 4, E = 5, F = 6, G = 7, H = 8 };

using Opcodes =
    tessera::value_list<Opcode, Opcode::A, Opcode::B, Opcode::C, Opcode::D, Opcode::E, Opcode::F, Opcode::G, Opcode::H>;

template<Opcode I>
std::int64_t weight() {
    return static_cast<std::int64_t>(I) * 3;
}

void reportValueDispatch(std::size_t iterations) {
    const Opcode keys[] = {Opcode::A, Opcode::D, Opcode::H, Opcode::C};
    const std::size_t operations = iterations * (sizeof(keys) / sizeof(keys[0]));

    std::int64_t total = 0;
    const double listNs = nanosecondsPerOperation(operations, [&] {
        for (std::size_t i = 0; i < iterations; ++i) {
            for (Opcode key : keys) {
                doNotOptimize(key);
                Opcodes::dispatch(key, [&]<Opcode I>() { total += weight<I>(); });
                doNotOptimize(total);
            }
        }
    });
    sink += total;

    std::unordered_map<Opcode, std::int64_t (*)()> table{
        {Opcode::A, &weight<Opcode::A>}, {Opcode::B, &weight<Opcode::B>}, {Opcode::C, &weight<Opcode::C>},
        {Opcode::D, &weight<Opcode::D>}, {Opcode::E, &weight<Opcode::E>}, {Opcode::F, &weight<Opcode::F>},
        {Opcode::G, &weight<Opcode::G>}, {Opcode::H, &weight<Opcode::H>}};

    total = 0;
    const double mapNs = nanosecondsPerOperation(operations, [&] {
        for (std::size_t i = 0; i < iterations; ++i) {
            for (Opcode key : keys) {
                doNotOptimize(key);
                total += table.at(key)();
                doNotOptimize(total);
            }
        }
    });
    sink += total;

    std::printf("## Runtime value to compile-time constant (%zu dispatches)\n\n", operations);
    std::printf("| dispatch                          | ns/lookup |\n");
    std::printf("|-----------------------------------|-----------|\n");
    std::printf("| value_list::dispatch              | %9.3f |\n", listNs);
    std::printf("| unordered_map of function pointers| %9.3f |\n\n", mapNs);
}

}  // namespace

int main() {
    std::printf("tessera %s — %s\n\n", TESSERA_VERSION_STRING, tessera::algebra_backend_name);
    reportMemory();
    reportSystemDispatch(2'000'000);
    reportValueDispatch(2'000'000);
    return static_cast<int>(sink & 0);
}
