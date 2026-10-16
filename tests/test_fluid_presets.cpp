#include <catch2/catch_test_macros.hpp>
#include "fluid_presets.h"
#include "config.h"
using namespace sideshow;
TEST_CASE("Fluid presets resolve names and isolate palettes and effects") {
    REQUIRE(fluidPresets().size() == 11);
    REQUIRE(findFluidPreset("Overhead"));
    CHECK(findFluidPreset("Overhead")->ink);
    CHECK_FALSE(findFluidPreset("LIGHT SHOW")->ink);
    CHECK(findFluidPreset("Light Show")->palette == FluidPalette::Spectrum);
    CHECK(findFluidPreset("Ink")->palette == FluidPalette::Mono);
    CHECK_FALSE(findFluidPreset("Ink")->bloom);
    CHECK_FALSE(findFluidPreset("Ink")->sunrays);
    CHECK(findFluidPreset("lava")->splatForce == 4500);
    CHECK(findFluidPreset("unknown") == nullptr);
}
TEST_CASE("Fluid configuration defaults and validated overrides") {
    auto defaults = parseConfig("").config;
    CHECK(defaults.visuals.mode == VisualMode::Milkdrop);
    auto loaded = parseConfig("[visuals]\nmode='fluid'\n[fluid]\npreset='lava'\nsimulation_resolution=32\ndye_resolution=1024\nambient_interval=0.1");
    CHECK(loaded.warnings.empty());
    CHECK(loaded.config.visuals.mode == VisualMode::Fluid);
    CHECK(loaded.config.fluid.preset == "lava");
    CHECK(loaded.config.fluid.simulationResolution == 32);
    CHECK(loaded.config.fluid.dyeResolution == 1024);
    for (const auto* value : {"31", "257", "128.5", "'128'", "nan", "inf"}) {
        auto bad = parseConfig(std::string("[fluid]\nsimulation_resolution=") + value);
        CHECK_FALSE(bad.warnings.empty());
        CHECK(bad.config.fluid.simulationResolution == 128);
    }
    for (const auto* value : {"127", "1025", "512.5", "'512'"}) {
        auto bad = parseConfig(std::string("[fluid]\ndye_resolution=") + value);
        CHECK_FALSE(bad.warnings.empty());
        CHECK(bad.config.fluid.dyeResolution == 512);
    }
    for (const auto* value : {"0", "61", "nan", "inf", "'2.5'"}) {
        auto bad = parseConfig(std::string("[fluid]\nambient_interval=") + value);
        CHECK_FALSE(bad.warnings.empty());
        CHECK(bad.config.fluid.ambientInterval == 2.5f);
    }
    CHECK_FALSE(parseConfig("[visuals]\nmode='browser'").warnings.empty());
    CHECK_FALSE(parseConfig("[fluid]\npreset='../foo'").warnings.empty());
}
