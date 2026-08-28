// Copyright 2026 Denis Tishkov
// SPDX-License-Identifier: Apache-2.0
//
// Example 3 — crossing back from runtime to compile time.
//
// The set of image formats a build supports is known when the binary is linked, but the format tag
// in an incoming file header is an ordinary runtime value. `value_list::dispatch` bridges the two:
// it finds the matching constant and hands it to the handler as a template argument, so the handler
// body can instantiate the per-format types that carry the magic number, the channel count and the
// decoder itself.
//
//     cmake --build build --target tessera_example_03_runtime_dispatch
//     ./build/examples/03_runtime_dispatch

#include <cstdio>
#include <string_view>

#include <tessera/tessera.hpp>

namespace {

enum class Format { Png = 1, Jpeg = 2, WebP = 3 };

/// Per-format compile-time definition — the kind of table that must not become runtime data.
template<Format F>
struct Traits;

template<>
struct Traits<Format::Png> {
    static constexpr std::string_view name = "PNG";
    static constexpr int channels = 4;
    static constexpr int headerBytes = 8;
};

template<>
struct Traits<Format::Jpeg> {
    static constexpr std::string_view name = "JPEG";
    static constexpr int channels = 3;
    static constexpr int headerBytes = 2;
};

template<>
struct Traits<Format::WebP> {
    static constexpr std::string_view name = "WebP";
    static constexpr int channels = 4;
    static constexpr int headerBytes = 12;
};

/// A decoder written for one format, i.e. what the dispatch has to reach.
template<Format F>
struct Decoder {
    using Def = Traits<F>;

    static void decode(int byteCount) {
        const int pixels = (byteCount - Def::headerBytes) / Def::channels;
        std::printf("  %.*s: %d bytes -> about %d pixels at %d channels\n", static_cast<int>(Def::name.size()),
                    Def::name.data(), byteCount, pixels, Def::channels);
    }
};

// The supported set, derived from the decoders that were linked in rather than written out by hand.
template<Format F>
struct Support {
    static constexpr Format format = F;
};

template<class T>
struct format_of {
    static constexpr Format value = T::format;
};

using LinkedDecoders = tessera::of<Support<Format::Png>, Support<Format::Jpeg>, Support<Format::WebP>,
                                   Support<Format::Png>>;  // duplicate: dropped at assembly

using SupportedFormats = LinkedDecoders::values<Format, format_of>;

static_assert(SupportedFormats::size == 3);
static_assert(SupportedFormats::contains(Format::WebP));

/// What a file looks like once its header has been sniffed.
struct File {
    Format format;
    int byteCount;
};

}  // namespace

int main() {
    const File inbox[] = {
        {Format::Png, 1'048'584},
        {Format::WebP, 262'156},
        {static_cast<Format>(99), 1024},  // not supported by this build
    };

    for (const File& file : inbox) {
        const bool handled = SupportedFormats::dispatch(file.format, [&]<Format F>() {
            // `F` is a compile-time constant again: Decoder<F> and Traits<F> are instantiated.
            Decoder<F>::decode(file.byteCount);
        });
        if (!handled) {
            std::printf("  unknown format %d — skipped\n", static_cast<int>(file.format));
        }
    }

    // The same list, reused to build a type: one decoder per supported format.
    using Decoders = SupportedFormats::into_types<tessera::mosaic, Decoder>;
    std::printf("\ndecoders instantiated: %zu\n", Decoders::size);

    return 0;
}
