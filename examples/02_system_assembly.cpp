// Copyright 2026 Denis Tishkov
// SPDX-License-Identifier: Apache-2.0
//
// Example 2 — what the library is actually for.
//
// Several independently written systems each declare the services they need. The engine collects
// the union of those declarations, deduplicates it, and instantiates each service exactly once.
// Nobody maintains a central list, no service is built twice, and a service that no system asks
// for any more disappears from the binary — all at compile time.
//
//     cmake --build build --target tessera_example_02_system_assembly
//     ./build/examples/02_system_assembly

#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>

#include <tessera/tessera.hpp>

namespace {

// ---- services ----------------------------------------------------------------------------------

struct Clock {
    long milliseconds = 0;

    void advance(long delta) {
        milliseconds += delta;
    }
};

struct FrameBuffer {
    int width = 0;
    int height = 0;

    [[nodiscard]] int pixels() const {
        return width * height;
    }
};

struct AssetCache {
    int loadedMeshes = 0;
};

struct InputState {
    bool jumpPressed = false;
};

struct Log {
    std::vector<std::string> lines;

    void write(std::string line) {
        lines.push_back(std::move(line));
    }
};

// ---- systems -----------------------------------------------------------------------------------

/// Every system answers one question: which services does it need?
template<class T>
using dependencies_of = typename T::dependencies;

struct Renderer {
    using dependencies = tessera::type_list<Clock, FrameBuffer, AssetCache>;

    template<class Engine>
    void step(Engine& engine) {
        const auto& frame = engine.template get<FrameBuffer>();
        const auto& assets = engine.template get<AssetCache>();
        engine.template get<Log>().write("renderer drew " + std::to_string(frame.pixels()) + " pixels from " +
                                         std::to_string(assets.loadedMeshes) + " meshes");
    }
};

struct Physics {
    using dependencies = tessera::type_list<Clock, InputState>;

    template<class Engine>
    void step(Engine& engine) {
        const auto& clock = engine.template get<Clock>();
        if (engine.template get<InputState>().jumpPressed) {
            engine.template get<Log>().write("physics started a jump at t=" + std::to_string(clock.milliseconds));
        }
    }
};

// ---- assembly ----------------------------------------------------------------------------------

using Systems = tessera::of<Renderer, Physics>;

// The union of every system's dependencies, deduplicated. Clock is named by both systems and exists
// once; Log is named by neither yet used by both — it is present because it was added explicitly.
using Engine = Systems::flat_map<dependencies_of>::add<Log>;

static_assert(std::is_same_v<Engine, tessera::mosaic<Clock, FrameBuffer, AssetCache, InputState, Log>>,
              "the assembled service set is fixed at compile time and can be asserted on");

}  // namespace

int main() {
    Engine engine;
    Systems systems;

    engine.get<FrameBuffer>() = FrameBuffer{.width = 1920, .height = 1080};
    engine.get<AssetCache>().loadedMeshes = 12;
    engine.get<InputState>().jumpPressed = true;
    engine.get<Clock>().advance(16);

    // One call site drives every system. `for_each` expands to a straight-line sequence of direct
    // calls — the system list is not a runtime container.
    systems.for_each([&](auto& system) { system.step(engine); });

    std::printf("services : %zu\n", Engine::size);
    std::printf("systems  : %zu\n\n", Systems::size);
    for (const std::string& line : engine.get<Log>().lines) {
        std::printf("  %s\n", line.c_str());
    }

    return 0;
}
