#include "fluid_backend.h"
namespace sideshow {
FluidBackend::FluidBackend(const BackendInit& i)
    :settings_(i.playback),ambientInterval_(i.config.fluid.ambientInterval),
     playback_(i.selection.preset,i.playback,std::random_device{}()),motion_(std::random_device{}()),
     renderer_(i.width,i.height,i.config.fluid){}
void FluidBackend::render(float dt) {
    playback_.update(dt);
    auto splats=seed_?motion_.seed(playback_.current()):motion_.update(dt,settings_.beatSensitivity,ambientInterval_,playback_.current());
    seed_=false;
    renderer_.render(dt,playback_.current(),splats);
}
}
