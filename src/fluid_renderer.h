#pragma once
#include "config.h"
#include "fluid_motion.h"
#include "fluid_gl.h"
#include <memory>
namespace sideshow {
class FluidRenderer {
public:
    FluidRenderer(int width,int height,const Config::Fluid& config);
    // Allows callers to choose a conservative subset of probed capabilities.
    FluidRenderer(int width,int height,const Config::Fluid& config,fluid_gl::Capabilities capabilities);
    ~FluidRenderer();
    FluidRenderer(const FluidRenderer&)=delete;FluidRenderer& operator=(const FluidRenderer&)=delete;
    void resize(int width,int height);
    void render(float dt,const FluidPreset& preset,const std::vector<FluidSplat>& splats);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
