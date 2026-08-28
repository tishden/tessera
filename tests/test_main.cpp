// Copyright 2026 Denis Tishkov
// SPDX-License-Identifier: Apache-2.0

#include <cstdio>

#include <tessera/tessera.hpp>

#include "check.hpp"

int main() {
    std::printf("tessera %s — dedup backend: %s\n\n", TESSERA_VERSION_STRING, tessera::dedup_backend_name);
    return tessera_test::run_all();
}
