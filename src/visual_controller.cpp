#include "visual_controller.h"
#include "log.h"
#include <stdexcept>
#include <type_traits>
namespace sideshow {
VisualController::VisualController(Config config,int width,int height,BackendFactory factory)
    : config_(std::move(config)), width_(width),height_(height),factory_(std::move(factory)) {
    settings_ = {config_.presets.duration,config_.presets.shuffle,false,
                 config_.presets.hardcutEnabled,config_.presets.beatSensitivity};
    saved_[0] = {config_.presets.playlist,""};
    saved_[1] = {"default",config_.fluid.preset};
    if (!setMode(config_.visuals.mode) && !setMode(VisualMode::Milkdrop))
        throw std::runtime_error("No visualization backend could initialize");
}
BackendInit VisualController::initializationFor(VisualMode m) const {
    return {config_,settings_,saved_[static_cast<size_t>(m)],width_,height_};
}
bool VisualController::setMode(VisualMode target) {
    if(target==VisualMode::Video) { logWarn("Video backend is not yet available"); return false; }
    if(active_ && mode_==target) return true;
    try {
        auto previous = active_ ? active_->selection() : VisualSelection{};
        auto candidate = factory_(target,initializationFor(target));
        if(!candidate) throw std::runtime_error("backend factory returned null");
        if(active_) saved_[static_cast<size_t>(mode_)]=std::move(previous);
        active_.swap(candidate); mode_=target;
        return true;
    } catch(const std::exception& e) {
        logWarn(std::string("Mode initialization failed: ")+e.what()); return false;
    }
}
void VisualController::recover() {
    if(mode_!=VisualMode::Fluid || !setMode(VisualMode::Milkdrop))
        throw std::runtime_error("Visualization recovery failed");
}
void VisualController::maintenance() {
    try {active_->maintenance();} catch(const std::exception& e) {logWarn(e.what());recover();}
}
void VisualController::render(float dt) {
    try {active_->render(dt);} catch(const std::exception& e) {
        logWarn(e.what());recover();active_->render(dt);
    }
}
void VisualController::resize(int w,int h) {
    if(w<=0 || h<=0) return;
    width_=w;height_=h;
    try {active_->resize(w,h);} catch(const std::exception& e) {logWarn(e.what());recover();}
}
void VisualController::addPcm(const float* pcm,size_t frames) {active_->addPcm(pcm,frames);}
std::string VisualController::statusPreset() const {
    return (mode_==VisualMode::Fluid ? "fluid/" : "") + active_->selection().preset;
}
void VisualController::apply(const Command& command) {
    std::visit([&](const auto& c) {
        using T=std::decay_t<decltype(c)>;
        if constexpr(std::is_same_v<T,cmd::Mode>) setMode(c.value);
        else if constexpr(std::is_same_v<T,cmd::Next>) active_->next();
        else if constexpr(std::is_same_v<T,cmd::Prev>) active_->prev();
        else if constexpr(std::is_same_v<T,cmd::Random>) active_->random();
        else if constexpr(std::is_same_v<T,cmd::Preset>) {
            if(!active_->selectPreset(c.name)) logWarn("Unknown or broken preset: "+c.name);
        } else if constexpr(std::is_same_v<T,cmd::Playlist>) {
            if(!active_->selectPlaylist(c.name)) logWarn("Unknown or empty playlist: "+c.name);
        } else if constexpr(std::is_same_v<T,cmd::Lock>) {
            settings_.locked=c.on;active_->setSettings(settings_);
        } else if constexpr(std::is_same_v<T,cmd::Duration>) {
            settings_.duration=c.seconds;active_->setSettings(settings_);
        } else if constexpr(std::is_same_v<T,cmd::HardCut>) {
            settings_.hardcut=c.on;active_->setSettings(settings_);
        } else if constexpr(std::is_same_v<T,cmd::BeatSensitivity>) {
            settings_.beatSensitivity=c.value;active_->setSettings(settings_);
        }
    },command);
}
}
