#include <catch2/catch_test_macros.hpp>
#include <numeric>

#include "Slic3r/PurgeOrder.hpp"
#include "Slic3r/Biz/Slicing/BackgroundProcess.hpp"
#include "fff_print/test_data.hpp"

using namespace Slic3r;

namespace {
std::vector<std::vector<unsigned>> layer_order(const Print& print)
{
    std::vector<std::vector<unsigned>> order;
    for (const LayerTools& layer : print.tool_ordering())
        order.push_back(layer.extruders);
    return order;
}

double tower_filament(const Print& print)
{
    const auto& used = print.wipe_tower_data()->used_filament_until_layer.back().second;
    return std::accumulate(used.begin(), used.end(), 0.);
}
}

TEST_CASE("MMU purge ordering compares complete towers and reverts after edits", "[Multi]")
{
    // Stacked blocks change the set of required tools between layers.
    const auto path = boost::filesystem::path(__FILE__).parent_path() / "data" / "mmu-purge.3mf";
    Domain::Model model;
    Domain::ConfigPack config;
    Biz::LegacyPresetMetadata profile;
    Biz::VirtualExtrudersConfig virtual_extruders;
    boost::optional<Semver> version;
    Domain::WipeTowersOnBeds towers;
    Domain::CustomGCodesOnBeds events;
    REQUIRE(Slic3rLegacy::load_3mf_legacy(path.string().c_str(), config, profile, &model,
        false, version, towers, events, virtual_extruders));
    auto& fdm = std::get<Domain::ConfigPackFDM>(config);
    fdm.printer.items.opt("printer_model").set(profile.printer_model);
    const Domain::Preset::SelectedPresetMetadata metadata = Test::create_dummy_selected_preset_metadata(
        Test::create_dummy_hw_config(fdm.tool.size()));
    Domain::Bed bed;
    Domain::BedInstance instance{bed};
    for (const auto* object : model.objects)
        for (auto* part : object->instances)
            instance.model_instances.push_back(part);
    Domain::CustomGCode::Info schedule{Domain::CustomGCode::Mode::MultiAsSingle, {}};
    instance.custom_gcode = schedule;
    auto slice = [&](Print& print) {
        const auto gcode_metadata = Biz::Slicing::build_gcode_metadata({}, metadata, fdm);
        const Biz::Slicing::UpdateResult result = print.update(model, config, instance, metadata,
            Biz::Slicing::build_metadata_serializer(gcode_metadata, metadata, fdm));
        REQUIRE_FALSE(std::holds_alternative<Biz::Slicing::ApplyStatus::InvalidData>(result.status));
        print.process();
        REQUIRE(print.wipe_tower_data().has_value());
    };

    Print optimized;
    slice(optimized);
    const auto optimized_order = layer_order(optimized);
    const double optimized_filament = tower_filament(optimized);
    const ToolOrdering reference(optimized, std::numeric_limits<unsigned>::max(), true);
    REQUIRE_FALSE(std::equal(reference.begin(), reference.end(), optimized.tool_ordering().begin(),
        [](const LayerTools& a, const LayerTools& b) { return a.extruders == b.extruders; }));

    {
        std::vector<LayerTools> layers{reference.front(), reference.back()};
        layers[0].extruders = {0, 1, 2};
        layers[1].extruders = {2, 1, 0};
        reorder_by_purge_volume(layers, {{0, 8, 5}, {9, 0, 6}, {3, 7, 0}});
        CHECK(layers[0].extruders == std::vector<unsigned>{0, 2, 1});
        CHECK(layers[1].extruders == std::vector<unsigned>{1, 2, 0});
        layers[0].extruders = {0, 1, 2};
        layers[1].extruders = {1};
        reorder_by_purge_volume(layers, {{0, 5, 5}, {5, 0, 5}, {5, 5, 0}});
        CHECK(layers[0].extruders == std::vector<unsigned>{0, 2, 1});
    }
    SECTION("Reordering saves filament but unloads the third filament more often") {
        fdm.filament.at(2).items.opt("filament_toolchange_delay").set(300.);
    }
    SECTION("A scheduled pause disables the alternative") {
        schedule.gcodes.push_back({0.4, Domain::CustomGCode::Type::PausePrint, 0, {}, {}});
        instance.custom_gcode = schedule;
    }
    SECTION("A printer hook disables the alternative") {
        auto& hook = fdm.printer.items.opt("before_layer_gcode");
        hook.set(hook.get<std::string>() + "\n; edited layer hook");
    }
    SECTION("A material eligibility edit disables the alternative") {
        fdm.filament.at(2).items.opt("filament_density").set(1.5);
    }

    slice(optimized);
    Print fresh;
    slice(fresh);
    const auto rebuilt = layer_order(optimized);
    REQUIRE(rebuilt != optimized_order);
    CHECK(rebuilt == layer_order(fresh));
    REQUIRE(rebuilt.size() == std::size_t(std::distance(reference.begin(), reference.end())));
    for (std::size_t i = 0; i < rebuilt.size(); ++i)
        CHECK(rebuilt[i] == (reference.begin() + i)->extruders);
    CHECK(tower_filament(optimized) > optimized_filament);
    const WipeTowerData& incremental = *optimized.wipe_tower_data();
    const WipeTowerData& expected = *fresh.wipe_tower_data();
    CHECK(incremental.used_filament_until_layer == expected.used_filament_until_layer);
    REQUIRE(incremental.tool_changes.size() == expected.tool_changes.size());
    for (std::size_t i = 0; i < incremental.tool_changes.size(); ++i) {
        REQUIRE(incremental.tool_changes[i].size() == expected.tool_changes[i].size());
        for (std::size_t j = 0; j < incremental.tool_changes[i].size(); ++j)
            CHECK(incremental.tool_changes[i][j].gcode == expected.tool_changes[i][j].gcode);
    }
    CHECK(incremental.final_purge->gcode == expected.final_purge->gcode);
}
