#ifndef slic3r_PurgeOrder_hpp_
#define slic3r_PurgeOrder_hpp_

#include <algorithm>
#include <iterator>
#include <limits>
#include <tuple>
#include <vector>

namespace Slic3r {

// Prefer the loaded filament, then the least expensive directed change. On
// equal costs leave a tool needed by the next layer available to finish last.
template<class Layers>
void reorder_by_purge_volume(Layers &layers, const std::vector<std::vector<float>> &volume)
{
    if (layers.empty() || layers.front().extruders.empty())
        return;
    unsigned loaded = layers.front().extruders.front();
    const unsigned last = layers.back().extruders.empty() ?
        std::numeric_limits<unsigned>::max() : layers.back().extruders.back();
    for (auto layer = layers.begin(); layer != layers.end(); ++layer) {
        const auto next = std::next(layer);
        auto rank = [&](unsigned tool) {
            return std::tuple{next == layers.end() && tool == last && tool != loaded,
                              tool == loaded ? -1.f : volume[loaded][tool],
                              next != layers.end() && next->has_extruder(tool), tool};
        };
        auto &tools = layer->extruders;
        for (auto it = tools.begin(); it != tools.end(); ++it) {
            auto cheapest = std::min_element(it, tools.end(), [&](unsigned a, unsigned b) { return rank(a) < rank(b); });
            std::iter_swap(it, cheapest);
            loaded = *it;
        }
    }
}

}
#endif
