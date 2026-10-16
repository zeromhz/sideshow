#pragma once
#include <string_view>
#include <vector>
namespace sideshow {
struct Color { float r, g, b; };
enum class FluidPalette { Spectrum, Warm, Mono, Custom };
struct FluidPreset {
    std::string_view slug, name;
    FluidPalette palette;
    std::vector<Color> colors;
    float monoHue, colorUpdateSpeed, curl, splatForce, splatRadius;
    float velocityDissipation, densityDissipation, pressure;
    int pressureIterations;
    bool colorful, shading, bloom, sunrays, ink;
    float bloomIntensity, bloomThreshold, bloomSoftKnee, sunraysWeight;
};
const std::vector<FluidPreset>& fluidPresets();
const FluidPreset* findFluidPreset(std::string_view name);
}
