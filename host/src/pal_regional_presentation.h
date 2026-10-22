#ifndef PS2HOST_PAL_REGIONAL_PRESENTATION_H
#define PS2HOST_PAL_REGIONAL_PRESENTATION_H

#include <bitset>
#include <cstddef>
#include <cstdint>

namespace pal_regional_presentation {

enum class Reject {
    None, Key, Video, Geometry, Uninitialized, Footprint, CpuWrite,
    PendingPatch, OtherProducer, Unknown
};

struct Candidate {
    std::uint32_t fbp = 0, fbw = 0, psm = 0, width = 0, height = 0;
    std::uint32_t scale = 0, shown = 0, dbx = 0, dby = 0;
    std::uint64_t pmode = 0, smode2 = 0, dispfb1 = 0, display1 = 0;
    std::uint64_t dispfb2 = 0, display2 = 0;
    bool initialized = false, footprint_known = false;
    bool cpu_write = false, pending_patch = false, other_producer = false;
    bool unknown = false;
    std::bitset<512> visible, stale;
};

// Certified PAL60 race layout only. Other video states retain the original
// presentation path until their geometry and coherence are validated.
inline Reject Evaluate(const Candidate& c) {
    if (c.fbp != 0 || c.fbw != 10 || c.psm != 0x0A) return Reject::Key;
    if (c.pmode != 0x66 || c.smode2 != 1 ||
        c.dispfb1 != 0 || c.display1 != 0 ||
        c.dispfb2 != 0x51400 || c.display2 != 0x1bf9ff0183227cull)
        return Reject::Video;
    if (c.width != 640 || c.height != 1024 ||
        (c.scale != 2 && c.scale != 3 && c.scale != 4) ||
        c.shown != 448 || c.dbx != 0 || c.dby != 0)
        return Reject::Geometry;
    if (!c.initialized) return Reject::Uninitialized;
    if (!c.footprint_known || c.visible.count() != 70 ||
        c.visible.test(70) || (c.visible & c.stale).any())
        return Reject::Footprint;
    for (std::size_t p = 0; p < 70; ++p)
        if (!c.visible.test(p)) return Reject::Footprint;
    if (c.cpu_write) return Reject::CpuWrite;
    if (c.pending_patch) return Reject::PendingPatch;
    if (c.other_producer) return Reject::OtherProducer;
    if (c.unknown) return Reject::Unknown;
    return Reject::None;
}

inline bool PhysicalExtentMatches(std::uint32_t scale, std::uint32_t width,
                                  std::uint32_t height) {
    return (scale == 2 || scale == 3 || scale == 4) &&
           width == 640u * scale && height == 1024u * scale;
}

// The observed PAL selector displays 512 lines. The direct bridge currently
// copies at most 448; retain Picture for this exact GS state.
inline bool SelectorNeedsFullHeightFallback(std::uint64_t pmode, std::uint64_t smode2,
                                            std::uint64_t dispfb1, std::uint64_t display1,
                                            std::uint64_t dispfb2, std::uint64_t display2) {
    return pmode == 0x66 && smode2 == 1 && dispfb1 == 0 && display1 == 0 &&
           dispfb2 == 0x51400 && display2 == 0x001ff9ff01848290ull;
}

}  // namespace pal_regional_presentation
#endif
