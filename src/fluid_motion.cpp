#include "fluid_motion.h"
#include <algorithm>
#include <cmath>
namespace sideshow {
float FluidMotion::unit() { return std::uniform_real_distribution<float>(0,1)(rng_); }
void FluidMotion::addPcm(const float* stereo, size_t frames) {
    if (!stereo || frames == 0) return;
    double sum = 0;
    for (size_t i=0;i<frames*2;++i) {
        float v = std::isfinite(stereo[i]) ? std::clamp(stereo[i],-1.0f,1.0f) : 0;
        sum += v*v;
    }
    // Retain an onset across the bounded chunks delivered in this frame only.
    energy_ = std::max(energy_, static_cast<float>(std::sqrt(sum/(frames*2))));
    fresh_ = true;
}
Color FluidMotion::color(const FluidPreset& p) {
    if (preset_ == p.slug && (!p.colorful || colorElapsed_*p.colorUpdateSpeed < 1)) return color_;
    preset_ = p.slug; colorElapsed_ = 0;
    if (p.palette == FluidPalette::Custom && !p.colors.empty()) {
        color_ = p.colors[std::uniform_int_distribution<size_t>(0,p.colors.size()-1)(rng_)];
    } else {
        float h = unit();
        if (p.palette == FluidPalette::Warm) h *= .13f;
        if (p.palette == FluidPalette::Mono) h = p.monoHue/360.0f;
        float f = h*6 - std::floor(h*6);
        switch (static_cast<int>(h*6)%6) {
        case 0: color_={1,f,0}; break; case 1: color_={1-f,1,0}; break;
        case 2: color_={0,1,f}; break; case 3: color_={0,1-f,1}; break;
        case 4: color_={f,0,1}; break; default: color_={1,0,1-f}; break;
        }
    }
    return color_;
}
FluidSplat FluidMotion::splat(const FluidPreset& p) {
    return {unit(),unit(),(unit()-.5f)*p.splatForce,(unit()-.5f)*p.splatForce,color(p)};
}
std::vector<FluidSplat> FluidMotion::seed(const FluidPreset& p) {
    std::vector<FluidSplat> result;
    for (int i=0;i<5;++i) { colorElapsed_=100; result.push_back(splat(p)); }
    return result;
}
std::vector<FluidSplat> FluidMotion::update(float dt, float sensitivity, float interval, const FluidPreset& p) {
    dt = std::isfinite(dt) ? std::clamp(dt,0.0f,60.0f) : 0;
    cooldown_ += dt; ambient_ += dt; colorElapsed_ += dt;
    float input = fresh_ ? energy_ : 0;
    fresh_ = false; energy_ = 0;
    const float oldBaseline = baseline_;
    envelope_ += (input-envelope_)*(1-std::exp(-dt*30));
    baseline_ += (input-baseline_)*(1-std::exp(-dt*1.5f));
    std::vector<FluidSplat> out;
    if (sensitivity > 0 && cooldown_ >= .12f && input*sensitivity > .18f &&
        (input-oldBaseline)*sensitivity > .12f) {
        int count = std::clamp(static_cast<int>(input*sensitivity*3),1,3);
        for (int i=0;i<count;++i) out.push_back(splat(p));
        cooldown_ = 0;
    }
    if (ambient_ >= interval) { out.push_back(splat(p)); ambient_=0; }
    return out;
}
}
