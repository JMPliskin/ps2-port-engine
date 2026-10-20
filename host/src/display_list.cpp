/*
 * Display lists blended (ps2host/display_list.h).
 */

#include "ps2host/display_list.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ps2host {
namespace {

struct Code {
    u32 at;         // the code's word
    u32 code;
    u32 words;      // its data's, after it
    bool floats;    // an UNPACK V4-32: four floats a vector
};

struct Segment {
    u64 key;        // its codes'
    u32 first, end; // its codes
};

/* A VIF code's data, in words after it. */
u32 DataWords(u32 code) {
    const u32 cmd = (code >> 24) & 0x7F;
    const u32 num = (code >> 16) & 0xFF;
    const u32 imm = code & 0xFFFF;
    if ((cmd & 0x60) == 0x60) {         // UNPACK: vn, vl - num vectors
        const u32 vl = cmd & 3, vn = (cmd >> 2) & 3;
        const u32 bits = (32u >> vl) * (vn + 1) * (num ? num : 256);
        return (bits + 31) / 32;
    }
    switch (cmd) {
    case 0x20: return 1;                // STMASK
    case 0x30: case 0x31: return 4;     // STROW, STCOL
    case 0x4A: return 2 * (num ? num : 256);                        // MPG
    case 0x50: case 0x51: return 4 * (imm ? imm : 65536);           // DIRECT, DIRECTHL
    default: return 0;
    }
}

bool Unpack(u32 code) { return ((code >> 24) & 0x60) == 0x60; }

void Parse(const std::vector<u32>& w, std::vector<Code>& codes) {
    codes.clear();
    size_t i = 0;
    while (i < w.size()) {
        const u32 code = w[i];
        const u32 words = DataWords(code);
        if (i + 1 + words > w.size()) break;
        const u32 cmd = (code >> 24) & 0x7F;
        codes.push_back({u32(i), code, words, Unpack(code) && (cmd & 0x0F) == 0x0C});
        i += 1 + words;
    }
}

void Split(const std::vector<Code>& codes, int address, std::vector<Segment>& segments) {
    segments.clear();
    u32 first = 0;
    u64 key = 1469598103934665603ull;
    for (u32 j = 0; j < codes.size(); ++j) {
        const u32 code = codes[j].code;
        // (an UNPACK's address with FLG set is the double buffer's: a batch of
        // vertices within a thing, not a thing's start)
        if (j > first && address >= 0 && Unpack(code) && !(code & 0x8000) && int(code & 0x3FF) == address) {
            segments.push_back({key, first, j});
            first = j;
            key = 1469598103934665603ull;
        }
        key = (key ^ code) * 1099511628211ull;
    }
    if (first < codes.size()) segments.push_back({key, first, u32(codes.size())});
}

bool Plausible(float v) { return v == 0.0f || (std::fabs(v) > 1e-6f && std::fabs(v) < 1e9f); }

float Float(u32 w) {
    float v;
    std::memcpy(&v, &w, 4);
    return v;
}

/* How far apart two segments' first UNPACKs are (each thing's matrices):
   0 the same, ~1 a value a word unlike; `words` the words that differ. */
float Distance(const std::vector<u32>& a, const Code& ca, const std::vector<u32>& b, const Code& cb, u32* words) {
    *words = 0;
    if (!ca.floats || ca.words != cb.words) return 0.0f;
    float d = 0.0f;
    for (u32 k = 1; k <= ca.words; ++k) {
        const u32 wa = a[ca.at + k], wb = b[cb.at + k];
        if (wa == wb) continue;
        ++*words;
        const float x = Float(wa), y = Float(wb);
        if (!Plausible(x) || !Plausible(y)) { d += 1.0f; continue; }
        d += std::fabs(y - x) / (std::fabs(x) + std::fabs(y) + 1.0f);
    }
    return d;
}

/* A matched pair's words to blend, added to `plan` - false (none added) if
   one jumps. */
bool PlanPair(const std::vector<u32>& a, const std::vector<Code>& ca, u32 sa, const std::vector<u32>& b,
              const std::vector<Code>& cb, const Segment& s, const ListBlendOptions& o, ListBlendPlan& plan) {
    const size_t mark = plan.at.size();
    for (u32 j = s.first; j < s.end; ++j) {
        const Code& y = cb[j];
        const Code& x = ca[sa + (j - s.first)];
        if (!y.floats || !y.words) continue;
        if (std::memcmp(&a[x.at + 1], &b[y.at + 1], y.words * 4) == 0) continue;
        float scale[4] = {0, 0, 0, 0};
        for (u32 k = 0; k < y.words; ++k) {
            const float fa = Float(a[x.at + 1 + k]), fb = Float(b[y.at + 1 + k]);
            if (Plausible(fa)) scale[k & 3] = std::max(scale[k & 3], std::fabs(fa));
            if (Plausible(fb)) scale[k & 3] = std::max(scale[k & 3], std::fabs(fb));
        }
        for (u32 k = 0; k < y.words; ++k) {
            const u32 wa = a[x.at + 1 + k], wb = b[y.at + 1 + k];
            if (wa == wb) continue;
            const float fa = Float(wa), fb = Float(wb);
            if (!Plausible(fa) || !Plausible(fb) || !(std::fabs(fb - fa) < o.cut)) continue;
            // (one word so is enough: a flickering light's billboard, its w
            // jumping, blended is a beam across the room)
            if (std::fabs(fb - fa) > o.jump * scale[k & 3]) {
                plan.at.resize(mark);
                plan.from.resize(mark);
                plan.to.resize(mark);
                return false;
            }
            plan.at.push_back(y.at + 1 + k);
            plan.from.push_back(fa);
            plan.to.push_back(fb);
        }
    }
    return true;
}

}  // namespace

ListBlend PlanListBlend(const Vif1Chain& a, const Vif1Chain& b, const ListBlendOptions& o, ListBlendPlan& plan) {
    plan.at.clear();
    plan.from.clear();
    plan.to.clear();
    ListBlend& r = plan.stats;
    r = ListBlend{};
    static thread_local std::vector<Code> ca, cb;
    static thread_local std::vector<Segment> sa, sb;
    Parse(a.words, ca);
    Parse(b.words, cb);
    Split(ca, o.segment_address, sa);
    Split(cb, o.segment_address, sb);
    r.codes = cb.size();
    r.segments = u32(sb.size());
    r.whole = ca.size() == cb.size() && std::equal(ca.begin(), ca.end(), cb.begin(),
                                                   [](const Code& x, const Code& y) { return x.code == y.code; });

    // The older list's segments by key, in order: (key, index).
    static thread_local std::vector<std::pair<u64, u32>> keys;
    keys.clear();
    for (u32 i = 0; i < sa.size(); ++i) keys.push_back({sa[i].key, i});
    std::sort(keys.begin(), keys.end());
    static thread_local std::vector<bool> used;
    used.assign(sa.size(), false);

    u32 expected = 0, matched = 0;
    bool camera_cut = false;        // (options.cut_segment's)
    for (u32 jb = 0; jb < sb.size(); ++jb) {
        const Segment& s = sb[jb];
        const bool camera = int(jb) == o.cut_segment;
        auto lo = std::lower_bound(keys.begin(), keys.end(), std::make_pair(s.key, 0u));
        auto hi = std::upper_bound(keys.begin(), keys.end(), std::make_pair(s.key, ~0u));
        // Its place first; else the nearest values among the unused nearby.
        u32 best = ~0u, best_words = 0;
        float best_d = 0.0f;
        const auto at = std::lower_bound(lo, hi, std::make_pair(s.key, expected));
        auto try_one = [&](u32 i) {
            if (used[i]) return;
            u32 words = 0;
            const float d = Distance(a.words, ca[sa[i].first], b.words, cb[s.first], &words);
            const bool nearer = best == ~0u || d < best_d ||
                                (d == best_d && (i > expected ? i - expected : expected - i) <
                                                    (best > expected ? best - expected : expected - best));
            if (nearer) { best = i; best_d = d; best_words = words; }
        };
        int n = 0;
        for (auto p = at; p != hi && n < 16; ++p, ++n) try_one(p->second);
        n = 0;
        for (auto p = at; p != lo && n < 16; ++n) try_one((--p)->second);
        // Out of its place, only a near match: one far (a thing whose own
        // match drew with other codes this step - more batches, its detail -
        // paired with another of the codes it now has) is not one.
        if (best != ~0u && best != expected && best_d > o.moved_distance * float(std::max(1u, best_words)))
            best = ~0u;
        if (best == ~0u) {
            ++r.unmatched;
            camera_cut |= camera;
            continue;
        }
        used[best] = true;
        ++matched;
        if (best != expected) ++r.moved;
        camera_cut |= camera && best != expected;
        expected = best + 1;
        if (!PlanPair(a.words, ca, sa[best].first, b.words, cb, s, o, plan)) {
            ++r.jumps;
            camera_cut |= camera;
        }
    }
    if (o.cut_segment >= 0 ? camera_cut : matched && float(r.jumps + r.moved) > o.cut_share * float(matched)) {
        r.cut = true;
        plan.at.clear();
        plan.from.clear();
        plan.to.clear();
    }
    r.blended = plan.at.size();
    return r;
}

void ApplyListBlend(const ListBlendPlan& plan, float t, std::vector<u32>& words, size_t offset) {
    for (size_t i = 0; i < plan.at.size(); ++i) {
        const size_t at = offset + plan.at[i];
        if (at >= words.size()) continue;
        const float v = plan.from[i] + t * (plan.to[i] - plan.from[i]);
        std::memcpy(&words[at], &v, 4);
    }
}

ListBlend BlendVif1Chains(const Vif1Chain& a, const Vif1Chain& b, float t, float cut, Vif1Chain& out) {
    ListBlendOptions options;
    options.cut = cut;
    ListBlendPlan plan;
    PlanListBlend(a, b, options, plan);
    out = b;
    ApplyListBlend(plan, t, out.words);
    return plan.stats;
}

}  // namespace ps2host
