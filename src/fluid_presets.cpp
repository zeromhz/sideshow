// Preset settings adapted from Chris Pirillo's Oilwheel; see third_party/fluid.
#include "fluid_presets.h"
namespace sideshow {
const std::vector<FluidPreset>& fluidPresets() {
    static const std::vector<FluidPreset> presets = {
        {"light-show", "Light Show", FluidPalette::Spectrum, {}, 0.0f, 12.0f, 30.0f, 6000.0f, 0.25f, 0.2f, 1.0f, 0.8f, 20, true, true, true, true, false, 0.8f, 0.6f, 0.7f, 1.0f},
        {"lava", "Lava", FluidPalette::Warm, {}, 0.0f, 4.0f, 12.0f, 4500.0f, 0.45f, 0.7f, 0.35f, 0.8f, 20, true, true, true, true, false, 1.3f, 0.4f, 0.8f, 1.0f},
        {"aurora", "Aurora", FluidPalette::Custom, {{0/255.0f, 255/255.0f, 163/255.0f}, {26/255.0f, 255/255.0f, 213/255.0f}, {58/255.0f, 255/255.0f, 160/255.0f}, {124/255.0f, 255/255.0f, 94/255.0f}, {0/255.0f, 212/255.0f, 255/255.0f}}, 0.0f, 5.0f, 14.0f, 5000.0f, 0.5f, 0.35f, 0.5f, 0.8f, 20, true, true, true, true, false, 1.1f, 0.45f, 0.9f, 1.0f},
        {"nebula", "Nebula", FluidPalette::Custom, {{181/255.0f, 26/255.0f, 255/255.0f}, {255/255.0f, 26/255.0f, 212/255.0f}, {94/255.0f, 26/255.0f, 255/255.0f}, {26/255.0f, 107/255.0f, 255/255.0f}, {255/255.0f, 77/255.0f, 255/255.0f}}, 0.0f, 7.0f, 26.0f, 7000.0f, 0.3f, 0.3f, 0.7f, 0.8f, 20, true, true, true, true, false, 1.2f, 0.5f, 0.7f, 1.0f},
        {"ember", "Ember", FluidPalette::Custom, {{255/255.0f, 34/255.0f, 0/255.0f}, {255/255.0f, 85/255.0f, 0/255.0f}, {255/255.0f, 136/255.0f, 0/255.0f}, {204/255.0f, 17/255.0f, 0/255.0f}}, 0.0f, 2.0f, 8.0f, 3800.0f, 0.4f, 0.9f, 0.6f, 0.8f, 20, true, true, true, true, false, 1.5f, 0.35f, 0.85f, 0.9f},
        {"neon", "Neon", FluidPalette::Custom, {{255/255.0f, 0/255.0f, 149/255.0f}, {0/255.0f, 255/255.0f, 242/255.0f}, {170/255.0f, 255/255.0f, 0/255.0f}, {255/255.0f, 0/255.0f, 255/255.0f}, {0/255.0f, 255/255.0f, 106/255.0f}}, 0.0f, 22.0f, 40.0f, 8500.0f, 0.2f, 0.15f, 1.1f, 0.8f, 20, true, true, true, true, false, 1.6f, 0.45f, 0.6f, 1.0f},
        {"oil-slick", "Oil Slick", FluidPalette::Custom, {{22/255.0f, 224/255.0f, 196/255.0f}, {255/255.0f, 43/255.0f, 181/255.0f}, {255/255.0f, 210/255.0f, 63/255.0f}, {122/255.0f, 77/255.0f, 255/255.0f}, {43/255.0f, 214/255.0f, 255/255.0f}}, 0.0f, 9.0f, 22.0f, 5500.0f, 0.32f, 0.4f, 0.7f, 0.8f, 20, true, true, true, true, false, 0.95f, 0.55f, 0.75f, 1.0f},
        {"ink", "Ink", FluidPalette::Mono, {}, 210.0f, 10.0f, 8.0f, 5500.0f, 0.22f, 1.2f, 0.6f, 0.8f, 20, false, true, false, false, false, 0.8f, 0.6f, 0.7f, 1.0f},
        {"frost", "Frost", FluidPalette::Custom, {{234/255.0f, 255/255.0f, 255/255.0f}, {168/255.0f, 230/255.0f, 255/255.0f}, {201/255.0f, 240/255.0f, 255/255.0f}, {127/255.0f, 212/255.0f, 255/255.0f}}, 0.0f, 3.0f, 6.0f, 3500.0f, 0.5f, 0.6f, 0.45f, 0.8f, 20, true, true, true, true, false, 0.9f, 0.6f, 0.95f, 0.8f},
        {"wild", "Wild", FluidPalette::Spectrum, {}, 0.0f, 25.0f, 50.0f, 11000.0f, 0.2f, 0.1f, 1.4f, 0.8f, 20, true, true, true, true, false, 1.5f, 0.5f, 0.6f, 1.0f},
        // Palette sampled from the user's Beyond Pompeii duotone banner.
        {"pompeii", "Pompeii", FluidPalette::Custom, {{34/255.0f, 0/255.0f, 241/255.0f}, {81/255.0f, 0/255.0f, 223/255.0f}, {134/255.0f, 1/255.0f, 202/255.0f}, {176/255.0f, 1/255.0f, 185/255.0f}, {216/255.0f, 0/255.0f, 169/255.0f}}, 0.0f, 4.0f, 12.0f, 4500.0f, 0.45f, 0.7f, 0.35f, 0.8f, 20, true, true, true, false, false, 0.6f, 0.6f, 0.8f, 1.0f},
        {"overhead", "Overhead", FluidPalette::Custom, {{255/255.0f, 122/255.0f, 0/255.0f}, {255/255.0f, 42/255.0f, 109/255.0f}, {122/255.0f, 60/255.0f, 255/255.0f}, {31/255.0f, 182/255.0f, 255/255.0f}, {35/255.0f, 209/255.0f, 139/255.0f}}, 0.0f, 6.0f, 16.0f, 4800.0f, 0.42f, 0.5f, 0.5f, 0.8f, 20, true, true, false, false, true, 0.8f, 0.6f, 0.7f, 1.0f}
    };
    return presets;
}
namespace {
bool equalName(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? c + ('a'-'A') : c; };
        if (lower(a[i]) != lower(b[i])) return false;
    }
    return true;
}
}
const FluidPreset* findFluidPreset(std::string_view name) {
    for (const auto& p : fluidPresets())
        if (equalName(name, p.slug) || equalName(name, p.name)) return &p;
    return nullptr;
}
}
