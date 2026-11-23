#include <catch2/catch_test_macros.hpp>

#include "libslic3r/GCode/PurgeOrder.hpp"

namespace {
struct Layer {
    std::vector<unsigned> extruders;
    bool has_extruder(unsigned tool) const { return std::find(extruders.begin(), extruders.end(), tool) != extruders.end(); }
};
}

TEST_CASE("MMU purge planning carries the loaded tool into the next layer", "[Multi]")
{
    const std::vector<std::vector<float>> matrix{{0, 8, 5}, {9, 0, 6}, {3, 7, 0}};
    std::vector<Layer> layers{{{0, 1, 2}}, {{2, 1, 0}}};
    SECTION("A cheaper transition changes both sides of a boundary") {
        Slic3r::reorder_by_purge_volume(layers, matrix);
        REQUIRE(layers[0].extruders == std::vector<unsigned>{0, 2, 1});
        REQUIRE(layers[1].extruders == std::vector<unsigned>{1, 2, 0});
    }
    SECTION("With uniform purge, keep a tool needed by the next layer until last") {
        layers[1].extruders = {1};
        Slic3r::reorder_by_purge_volume(layers, {{0, 5, 5}, {5, 0, 5}, {5, 5, 0}});
        REQUIRE(layers[0].extruders == std::vector<unsigned>{0, 2, 1});
        REQUIRE(layers[1].extruders == std::vector<unsigned>{1});
    }
}
