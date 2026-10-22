#include "../src/texture_page_index.h"
#include <cstdio>
#include <limits>
#include <unordered_map>

namespace {
struct Texture {
    unsigned begin = 0, end = 0;
    bool valid = false, indexed = false;
};
using Index = ps2host::detail::TexturePageIndex<Texture>;
int failures = 0;
void Check(bool ok, const char* name) {
    if (!ok && failures++ < 12) std::fprintf(stderr, "FAIL: %s\n", name);
}
std::size_t Entries(const Index& index) {
    std::size_t count = 0;
    for (const auto& page : index.pages) count += page.size();
    return count;
}

void BoundariesAndAliases() {
    Index index;
    Texture a{31, 65}, alias{32, 64}, beside{0, 1}, last{16383, 16384};
    Check(Entries(index) == 0 && !a.indexed, "cold invalid entries stay unindexed");
    index.Validated(&a); index.Validated(&a);
    Check(Entries(index) == 3, "multi-page insertion is idempotent");
    index.Validated(&alias); index.Validated(&beside); index.Validated(&last);
    index.Invalidate(31, 32);
    Check(!a.valid && !a.indexed && alias.valid && beside.valid,
          "overlap is block-exact within one page; half-open endpoint");
    Check(index.pages[2].empty(), "one write removes an entry from every page");
    index.Invalidate(32, 64);
    Check(!alias.valid && !alias.indexed, "an alias is invalidated");
    index.Validated(&a); index.Validated(&alias);
    index.Invalidate(32, 33);
    Check(!a.valid && !alias.valid, "all overlapping aliases leave the index");
    index.Invalidate(0, 0); index.Invalidate(20, 10);
    index.Invalidate(16384, 16384); index.Invalidate(16384, 17000);
    Check(beside.valid && last.valid, "empty, reversed and out-of-memory writes do nothing");
    index.Invalidate(16383, std::numeric_limits<unsigned>::max());
    Check(!last.valid && !last.indexed, "last GS page and clipped write endpoint");
    index.Invalidate(&beside); index.Invalidate(&beside); index.Index(&beside, false);
    Check(Entries(index) == 0, "duplicate removal is harmless");
    a.begin = 100; a.end = 200;
    index.Validated(&a);
    Check(a.valid && a.indexed && Entries(index) == 4, "revalidation and changed-size reuse");
    index.Invalidate(&a);
    Check(Entries(index) == 0, "resized entry is fully removed");
}

void LifetimeAndRepeatedWrites() {
    Index index;
    std::unordered_map<unsigned, Texture> cache;
    for (unsigned i = 0; i < 4096; ++i) {
        auto& t = cache[i]; t.begin = 100; t.end = 160;
        index.Validated(&t);
    }
    index.Invalidate(100, 160);
    Check(Entries(index) == 0 && cache.size() == 4096,
          "invalid cache history costs no page candidates on subsequent writes");
    index.Invalidate(100, 160);
    auto& current = cache[22]; index.Validated(&current);
    Check(Entries(index) == 2, "only a revalidated texture returns");
    index.Index(&current, false); cache.erase(22);
    index.Invalidate(0, 16384);
    Check(Entries(index) == 0, "eviction removes pointers before object destruction");
}

// Independent global interval model: varied register/PSM aliases reach this
// index as block ranges. Compare validity and every page membership after
// writes, GPU/replacement invalidation, revalidation and entry reuse.
void DifferentialSequence() {
    Index index;
    std::array<Texture, 128> textures{};
    std::array<bool, 128> expected{};
    unsigned rng = 0x01052584;
    auto next = [&] { rng = rng * 1664525u + 1013904223u; return rng; };
    for (auto& t : textures) {
        t.begin = next() % 16384;
        t.end = std::min(16384u, t.begin + 1 + next() % 192);
    }
    for (unsigned step = 0; step < 2000; ++step) {
        unsigned selected = next() % textures.size();
        auto& t = textures[selected];
        switch ((next() >> 16) % 4) {
        case 0:
            index.Validated(&t); index.Validated(&t); expected[selected] = true; break;
        case 1:
            index.Invalidate(&t); expected[selected] = false; break;
        case 2:
            index.Invalidate(&t); expected[selected] = false;
            t.begin = next() % 16384;
            t.end = std::min(16384u, t.begin + 1 + next() % 192);
            break;
        default: {
            unsigned begin = next() % 16385, end = std::min(16384u, begin + next() % 256);
            index.Invalidate(begin, end);
            for (unsigned n = 0; n < textures.size(); ++n)
                if (begin < end && textures[n].begin < end && begin < textures[n].end) expected[n] = false;
        }}
        for (unsigned n = 0; n < textures.size(); ++n) {
            const auto& entry = textures[n];
            Check(entry.valid == expected[n] && entry.indexed == expected[n], "validity/index invariant");
            for (unsigned page = 0; page < 512; ++page) {
                const auto& list = index.pages[page];
                const auto found = std::count(list.begin(), list.end(), &entry);
                const bool want = expected[n] && page >= entry.begin / 32 && page <= (entry.end - 1) / 32;
                Check(found == (want ? 1 : 0), "exact page membership without duplicates");
            }
        }
    }
}
}

int main() {
    BoundariesAndAliases(); LifetimeAndRepeatedWrites(); DifferentialSequence();
    std::printf("Texture page index: %s (2000 differential operations)\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
