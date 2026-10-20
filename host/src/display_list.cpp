/*
 * Display lists blended (ps2host/display_list.h).
 */

#include "ps2host/display_list.h"

#include <cmath>
#include <cstring>

namespace ps2host {
namespace {

/* A VIF code's data, in words after it (and whether it is an UNPACK's). */
u32 DataWords(u32 code, bool& unpack) {
    const u32 cmd = (code >> 24) & 0x7F;
    const u32 num = (code >> 16) & 0xFF;
    const u32 imm = code & 0xFFFF;
    unpack = false;
    if ((cmd & 0x60) == 0x60) {         // UNPACK: vn, vl - num vectors
        unpack = true;
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

bool Plausible(float v) { return v == 0.0f || (std::fabs(v) > 1e-6f && std::fabs(v) < 1e9f); }

}  // namespace

ListBlend BlendVif1Chains(const Vif1Chain& a, const Vif1Chain& b, float t, float cut, Vif1Chain& out) {
    ListBlend result;
    out = b;
    const size_t n = std::min(a.words.size(), b.words.size());
    size_t i = 0;
    while (i < n) {
        const u32 ca = a.words[i], cb = b.words[i];
        if (ca != cb) return result;    // (the rest is b's)
        bool unpack = false;
        const u32 words = DataWords(cb, unpack);
        ++result.codes;
        ++i;
        if (i + words > n) return result;
        if (unpack)
            for (u32 k = 0; k < words; ++k) {
                float fa, fb;
                std::memcpy(&fa, &a.words[i + k], 4);
                std::memcpy(&fb, &b.words[i + k], 4);
                if (fa == fb || !Plausible(fa) || !Plausible(fb) || !(std::fabs(fb - fa) < cut)) continue;
                const float blended = fa + t * (fb - fa);
                std::memcpy(&out.words[i + k], &blended, 4);
                ++result.blended;
            }
        i += words;
    }
    result.whole = a.words.size() == b.words.size();
    return result;
}

}  // namespace ps2host
