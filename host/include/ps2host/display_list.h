#pragma once
/*
 * A game's display lists - the VIF1 chains it sends each picture (Vif1Chain,
 * SetVif1ChainFilter) - blended: a picture between two of its steps made from
 * the lists of the two, for a game whose drawing is written into one list
 * while its update runs (Resident Evil Outbreak), where no draw can be run
 * apart from the step (skill ps2-high-fps).
 *
 * Each list is cut into the things it draws - a segment from each UNPACK to
 * one VU address (`segment_address`: where the game puts each thing's
 * matrices; not one to the double buffer, FLG) to the next. Each of the newer list's segments is matched with
 * one of the older list's of the same VIF codes - the one in its place, or,
 * where the game drew its things in another order (two billboards that
 * traded places), the one whose first values are nearest. In a matched pair
 * the UNPACK data that are plausible floats (0, or 1e-6 to 1e9 either way - a
 * projection's translations reach 2e7: a GIF tag, a count, a colour read as
 * a far smaller or odd float) and differ by less than `cut` are blended -
 * unless one of the pair's values jumps by more than `jump` of the largest
 * in its lane (x, y, z or w) of its UNPACK: a different thing in that place,
 * a sign turned over (w through 0: vertices thrown across the screen), a
 * flickering light's billboard. That segment (stepping as the game does), a
 * segment with no match (what came into the picture) and every
 * other word are the newer list's. Where many segments jump or move at once
 * (`cut_share` of them) the camera has cut: nothing is blended.
 *
 * A pair's blend is planned once (PlanListBlend: the words to blend, from and
 * to) and made for each picture between the two steps (ApplyListBlend).
 */

#include "ps2host/run.h"

namespace ps2host {

struct ListBlendOptions {
    float cut = 100000.0f;      // a value changing by more: not motion (not blended)
    int segment_address = 0;    // a segment starts at each UNPACK to this VU address (-1: one segment)
    float jump = 0.5f;          // of a lane's largest value: a jump - the segment not blended
    float cut_share = 0.2f;     // segments jumping or moved, of those matched: the camera cut
};

struct ListBlend {
    u64 codes = 0;          // VIF codes in the newer list
    u64 blended = 0;        // words blended
    u32 segments = 0;       // the newer list's segments
    u32 moved = 0;          // matched out of their place
    u32 unmatched = 0;      // with no match
    u32 jumps = 0;          // matched but jumping: not blended
    bool cut = false;       // the camera cut: nothing blended
    bool whole = false;     // the lists' codes the same to their end
};

struct ListBlendPlan {
    std::vector<u32> at;    // the newer list's words to blend
    std::vector<float> from, to;
    ListBlend stats;
};

/* The blend of `a` (older) into `b` (newer), planned. */
ListBlend PlanListBlend(const Vif1Chain& a, const Vif1Chain& b, const ListBlendOptions& options, ListBlendPlan& plan);

/* `words` (b's own, from `offset` in them) made `t` of the way from a's (0:
   a's values, 1: b's). */
void ApplyListBlend(const ListBlendPlan& plan, float t, std::vector<u32>& words, size_t offset = 0);

/* Both at once: `out` = `b` blended `t` of the way from `a`. */
ListBlend BlendVif1Chains(const Vif1Chain& a, const Vif1Chain& b, float t, float cut, Vif1Chain& out);

}  // namespace ps2host
