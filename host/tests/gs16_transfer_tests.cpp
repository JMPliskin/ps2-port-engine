// Test the actual translation unit without linking unrelated host/VU tests.
#define PS2HOST_GS16_TEST
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif
#include "../src/gs_memory.cpp"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#if defined(PS2HOST_GS16_EXPECT_SCALAR)
static_assert(PS2HOST_GS16_SIMD == 0, "scalar build must not compile the SSE4.1 path");
#elif defined(PS2HOST_GS16_EXPECT_SIMD)
static_assert(PS2HOST_GS16_SIMD == 1, "SIMD build must compile the selected path");
#endif

namespace {
using namespace ps2host::gs;
constexpr std::size_t bytes = 4u << 20;
unsigned failures = 0, cases = 0, rng = 0x52584002;
std::uint64_t tested_pixels = 0, tested_ct16 = 0;
unsigned Next() { rng = rng * 1664525u + 1013904223u; return rng; }
void Check(bool ok, const char* what) {
    if (!ok && failures++ < 10) std::fprintf(stderr, "FAIL: %s\n", what);
}
void Reference(u8* memory, u32 bp, u32 bw, u32 x, u32 y, u32 psm, const u32* values, u32 count) {
    for (u32 n = 0; n < count; ++n) {
        u32 nibble;
        const u32 address = Memory::AddressReference(bp, bw, x + n, y, psm, &nibble);
        memory[address] = u8(values[n]);
        memory[address + 1] = u8(values[n] >> 8);
    }
}

void Differential() {
    Memory actual;
    std::vector<u8> expected(bytes);
    for (std::size_t n = 0; n < bytes; ++n) actual.data()[n] = expected[n] = u8(n * 73 + (n >> 11));
    auto one = [&](u32 bp, u32 bw, u32 x, u32 y, u32 psm, u32 count, u32 input_offset) {
        std::vector<u32> input(count + 4);
        u32* values = input.data() + input_offset;
        for (u32 n = 0; n < count; ++n) values[n] = (n & 3) ? Next() : 0xFFFF8000u + n;
        const auto before = g_gs16_groups;
        actual.WriteRun(bp, bw, x, y, psm, count ? values : nullptr, count);
        Reference(expected.data(), bp, bw, x, y, psm, values, count);
        Check(std::memcmp(actual.data(), expected.data(), bytes) == 0, "all 4 MiB match independent scalar address/store");
        const u32 aligned = (x + 15u) & ~15u;
        const u32 groups = psm == PSMCT16 && count >= aligned - x ? (count - (aligned - x)) / 16 : 0;
        Check(g_gs16_groups - before == (PS2HOST_GS16_SIMD ? groups : 0), "SIMD eligibility and scalar rejection");
        ++cases; tested_pixels += count; if (psm == PSMCT16) tested_ct16 += count;
    };
    const u32 lengths[] = {0, 1, 7, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127};
    const u32 starts[] = {0, 1, 7, 15, 16, 17, 31, 47, 48, 49, 63, 64, 65, 127};
    for (u32 count : lengths)
        for (u32 x : starts)
            for (u32 input = 0; input < 4; ++input)
                one(input & 1 ? 16383 : input, input & 1 ? 10 : 0, x,
                    (x + count) % 64, PSMCT16, count, input);
    for (u32 psm : {PSMCT16, PSMCT16S, PSMZ16, PSMZ16S})
        for (u32 y = 0; y < 64; ++y)
            one(16352 + y % 32, 1 + y % 10, 48, y, psm, 33, y % 4);
    // Partial rectangles, including row/page transitions and the final GS row.
    for (u32 y : {7u, 8u, 31u, 32u, 63u, 64u, 127u, 2047u})
        one(16383, 10, 2027, y, PSMCT16, 21, y % 4);
    for (unsigned n = 0; n < 512; ++n) {
        const u32 formats[] = {PSMCT16, PSMCT16S, PSMZ16, PSMZ16S};
        const u32 x = (Next() >> 8) % 2048;
        const u32 count = std::min(2048u - x, (Next() >> 8) % 385);
        one((Next() >> 8) % 16384, (Next() >> 8) % 64, x, (Next() >> 8) % 2048,
            formats[(Next() >> 8) % 4], count, (Next() >> 8) % 4);
    }
    Check(g_page_tables.ct16.pairs16, "every PSMCT16 table group satisfies the layout proof");
    Check(!g_page_tables.ct16s.pairs16 && !g_page_tables.z16.pairs16 && !g_page_tables.z16s.pairs16,
          "uncertified formats never enable SIMD");
    PageTable rejected = g_page_tables.ct16; rejected.pairs16 = false;
    std::array<u32, 32> input{}; for (auto& v : input) v = Next();
    const auto before = g_gs16_groups;
    StoreRun<PSMCT16, true>(actual.data(), rejected, 16383, 10, 0, 63, input.data(), 32);
    Reference(expected.data(), 16383, 10, 0, 63, PSMCT16, input.data(), 32);
    Check(g_gs16_groups == before && std::memcmp(actual.data(), expected.data(), bytes) == 0,
          "failed geometry certification uses scalar stores");
    // Existing non-16-bit dispatch is unchanged, checked without a format oracle redesign.
    for (u32 psm : {PSMCT32, PSMCT24, PSMZ32, PSMZ24, PSMT8, PSMT4, PSMT8H, PSMT4HL, PSMT4HH}) {
        Memory per_pixel, run;
        run.WriteRun(16383, 10, 47, 63, psm, input.data(), 32);
        for (u32 n = 0; n < 32; ++n) per_pixel.Write(16383, 10, 47 + n, 63, psm, input[n]);
        Check(std::memcmp(per_pixel.data(), run.data(), bytes) == 0, "other formats retain per-pixel semantics");
    }
    std::printf("GS16 differential: %u cases, %llu pixels, %llu CT16 pixels, %llu SIMD groups; ISA path %d\n",
                cases, (unsigned long long)tested_pixels, (unsigned long long)tested_ct16,
                (unsigned long long)g_gs16_groups, PS2HOST_GS16_SIMD);
}

void Guards() {
#if defined(_WIN32)
    // A short unaligned-to-16 input ends exactly at an inaccessible page.
    // The destination's complete GS memory also ends at an inaccessible page.
    auto reserve = [](std::size_t size) {
        auto* p = static_cast<u8*>(VirtualAlloc(nullptr, size + 8192, MEM_RESERVE, PAGE_NOACCESS));
        if (!p || !VirtualAlloc(p + 4096, size, MEM_COMMIT, PAGE_READWRITE)) std::abort();
        return p;
    };
    u8* source = reserve(4096); u8* destination = reserve(bytes);
    auto* input = reinterpret_cast<u32*>(source + 8192 - 17 * sizeof(u32));
    for (unsigned n = 0; n < 17; ++n) input[n] = Next();
    std::memset(destination + 4096, 0xA5, bytes);
    std::vector<u8> reference(bytes, 0xA5);
    for (u32 bp : {0u, 16352u, 16383u})
        for (u32 y : {0u, 7u, 63u, 64u}) {
            StoreRun<PSMCT16, true>(destination + 4096, g_page_tables.ct16, bp, 1, 48, y, input, 17);
            Reference(reference.data(), bp, 1, 48, y, PSMCT16, input, 17);
            Check(std::memcmp(destination + 4096, reference.data(), bytes) == 0, "guarded input/output and GS wrap");
        }
    VirtualFree(source, 0, MEM_RELEASE); VirtualFree(destination, 0, MEM_RELEASE);
#endif
}

int Benchmark() {
    Memory scalar, simd; std::array<u32, 640> input{};
    for (auto& v : input) v = Next();
    const auto& table = g_page_tables.ct16;
    for (unsigned trial = 0; trial < 5; ++trial) {
        double times[2]{};
        for (unsigned pass = 0; pass < 2; ++pass) {
            const bool optimized = ((pass + trial) & 1) != 0;
            const auto start = std::chrono::steady_clock::now();
            for (u32 n = 0; n < 20000; ++n)
                if (optimized) simd.WriteRun(0, 10, 0, n % 448, PSMCT16, input.data(), 640);
                else StoreRun<PSMCT16>(scalar.data(), table, 0, 10, 0, n % 448, input.data(), 640);
            times[optimized] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        }
        Check(std::memcmp(scalar.data(), simd.data(), bytes) == 0, "benchmark output matches");
        std::printf("GS16 microbench trial %u: scalar %.3f ms, selected %.3f ms (12800000 pixels each)\n",
                    trial, times[0], times[1]);
    }
    return failures ? 1 : 0;
}
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--bench") return Benchmark();
    Differential(); Guards();
    Check(PS2HOST_GS16_SIMD ? g_gs16_groups > 0 : g_gs16_groups == 0, "selected ISA path exercised");
    std::printf("GS16 transfers: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
