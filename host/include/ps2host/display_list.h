#pragma once
/*
 * A game's display lists - the VIF1 chains it sends each picture (Vif1Chain,
 * SetVif1ChainFilter) - blended: a picture between two of its steps made from
 * the lists of the two, for a game whose drawing is written into one list
 * while its update runs (Resident Evil Outbreak), where no draw can be run
 * apart from the step (skill ps2-high-fps).
 *
 * The two lists are walked together, VIF code by VIF code. Where they hold
 * the same code, its data are the same size and the code is an UNPACK
 * (data for VU memory: matrices, vertices, the values a VU program
 * transforms with), each pair of words that are both plausible floats (0, or
 * 1e-6 to 1e9 either way - a projection's translations reach 2e7: a GIF
 * tag, a count, a colour read as a far smaller or odd float) and differ by
 * less than `cut` is blended; every other word is the newer list's. From the first code that differs on, the newer list's
 * alone (what moved in or out of the picture).
 */

#include "ps2host/run.h"

namespace ps2host {

struct ListBlend {
    u64 codes = 0;          // VIF codes walked together
    u64 blended = 0;        // words blended
    bool whole = false;     // the two lists matched to their end
};

/* `out` = `b` with its float data `t` of the way from `a` (0: a's, 1: b's). */
ListBlend BlendVif1Chains(const Vif1Chain& a, const Vif1Chain& b, float t, float cut, Vif1Chain& out);

}  // namespace ps2host
