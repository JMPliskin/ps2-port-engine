#ifndef PS2HOST_TEXTURE_PAGE_INDEX_H
#define PS2HOST_TEXTURE_PAGE_INDEX_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ps2host::detail {

// GS-local-memory block ranges, independent of PSM. Entries have stable
// addresses and begin/end, valid and indexed fields. Like the bridge cache,
// this index belongs to the GS thread; the renderer thread never accesses it.
template<class Texture>
struct TexturePageIndex {
    std::array<std::vector<Texture*>, 512> pages;

    // Remove before changing an entry's range or destroying it. The bridge's
    // cache keys fix the range for the entry's lifetime.
    void Index(Texture* t, bool add) {
        if (t->indexed == add) return;
        t->indexed = add;
        const std::uint32_t last = t->end > t->begin ? (t->end - 1) >> 5 : t->begin >> 5;
        for (std::uint32_t page = t->begin >> 5; page <= last && page < pages.size(); ++page) {
            auto& list = pages[page];
            if (add) list.push_back(t);
            else {
                auto found = std::find(list.begin(), list.end(), t);
                if (found != list.end()) {
                    *found = list.back();
                    list.pop_back();
                }
            }
        }
    }

    void Invalidate(Texture* t) {
        t->valid = false;
        Index(t, false);
    }

    void Validated(Texture* t) {
        t->valid = true;
        Index(t, true);
    }

    void Invalidate(std::uint32_t begin, std::uint32_t end) {
        if (begin >= end) return;
        for (std::uint32_t page = begin >> 5; page <= ((end - 1) >> 5) && page < pages.size(); ++page) {
            auto& list = pages[page];
            for (std::size_t i = 0; i < list.size();) {
                Texture* t = list[i];
                // Removing from every page swaps this page's last entry into
                // i. Inspect it next; no temporary list or duplicate removals.
                if (t->begin < end && begin < t->end) Invalidate(t);
                else ++i;
            }
        }
    }
};

} // namespace ps2host::detail
#endif
