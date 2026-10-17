#include "visual_controller.h"
#include "log.h"
#include <stdexcept>
#include <type_traits>
namespace sideshow {
VisualController::VisualController(Config config,int width,int height,BackendFactory factory)
    : config_(std::move(config)), width_(width),height_(height),factory_(std::move(factory)) {
    settings_ = {config_.presets.duration,config_.presets.shuffle,config_.presets.locked,
                 config_.presets.hardcutEnabled,config_.presets.beatSensitivity,config_.fluid.persistence};
    saved_[0] = {config_.presets.playlist,""};
    saved_[1] = {"default",config_.fluid.preset};
    saved_[2] = {".",config_.video.preset};
    saved_[3] = {".",config_.isf.preset};
    videoEnd_=config_.video.end;
    if(config_.visuals.mode==VisualMode::Video || config_.visuals.mode==VisualMode::Isf) {
        if(!setMode(VisualMode::Milkdrop)) throw std::runtime_error("No startup fallback backend");
        setMode(config_.visuals.mode);return;
    }
    if (!setMode(config_.visuals.mode) && !setMode(VisualMode::Milkdrop))
        throw std::runtime_error("No visualization backend could initialize");
}
BackendInit VisualController::initializationFor(VisualMode m) const {
    return {config_,settings_,saved_[static_cast<size_t>(m)],width_,height_,videoEnd_};
}
void VisualController::activate(std::unique_ptr<VisualBackend> candidate,VisualMode target) {
    if(active_) {
        saved_[static_cast<size_t>(mode_)]=active_->selection();
        if(auto control=active_->videoControl()) videoEnd_=control->end();
    }
    active_=std::move(candidate);mode_=target;
}
bool VisualController::setMode(VisualMode target) {
    if(pending_ && target==pendingMode_) return true;
    pending_.reset();
    if(active_ && mode_==target) return true;
    try {
        auto candidate=factory_(target,initializationFor(target));
        if(!candidate) throw std::runtime_error("backend factory returned null");
        auto result=candidate->preparation();
        if(result.state==PreparationState::Failed) throw std::runtime_error(result.error);
        if(result.state==PreparationState::Pending) {pending_=std::move(candidate);pendingMode_=target;}
        else activate(std::move(candidate),target);
        return true;
    } catch(const std::exception& e) {
        logWarn(std::string("Mode initialization failed: ")+e.what());return false;
    }
}
void VisualController::recover() {
    if(mode_==VisualMode::Milkdrop || !setMode(VisualMode::Milkdrop))
        throw std::runtime_error("Visualization recovery failed");
}
void VisualController::maintenance() {
    try {active_->maintenance();} catch(const std::exception& e) {logWarn(e.what());recover();}
    if(pending_) {
        try {
            pending_->maintenance();
            auto result=pending_->preparation();
            if(result.state==PreparationState::Ready) activate(std::move(pending_),pendingMode_);
            else if(result.state==PreparationState::Failed) {logWarn(result.error);pending_.reset();}
        } catch(const std::exception& e) {logWarn(e.what());pending_.reset();}
    }
}
void VisualController::render(float dt) {
    try {active_->render(dt);} catch(const std::exception& e) {
        logWarn(e.what());recover();active_->render(dt);
    }
}
void VisualController::resize(int w,int h) {
    if(w<=0 || h<=0) return;
    width_=w;height_=h;
    if(pending_) { try {pending_->resize(w,h);} catch(const std::exception& e) {logWarn(e.what());pending_.reset();} }
    try {active_->resize(w,h);} catch(const std::exception& e) {logWarn(e.what());recover();}
}
void VisualController::addPcm(const float* pcm,size_t frames) {active_->addPcm(pcm,frames);if(pending_)pending_->addPcm(pcm,frames);}
void VisualController::clearAudioHistory(){active_->clearAudioHistory();if(pending_)pending_->clearAudioHistory();}
std::string VisualController::statusPreset() const {
    return (mode_==VisualMode::Isf ? "isf/" : mode_==VisualMode::Video ? "video/" : mode_==VisualMode::Fluid ? "fluid/" : "") + active_->selection().preset;
}
void VisualController::updateSettings() {
    active_->setSettings(settings_);
    if(pending_) pending_->setSettings(settings_);
}
void VisualController::apply(const Command& command) {
    auto* target=pending_ ? pending_.get() : active_.get();
    std::visit([&](const auto& c) {
        using T=std::decay_t<decltype(c)>;
        if constexpr(std::is_same_v<T,cmd::Mode>) setMode(c.value);
        else if constexpr(std::is_same_v<T,cmd::IsfParam> || std::is_same_v<T,cmd::IsfEvent>) {
            if(auto control=target->isfControl()) {
                if constexpr(std::is_same_v<T,cmd::IsfParam>) control->setParameter(c);
                else control->triggerEvent(c);
            } else logWarn("ISF control ignored outside ISF mode");
        }
        else if constexpr(std::is_same_v<T,cmd::VideoEndMode> || std::is_same_v<T,cmd::VideoPause> || std::is_same_v<T,cmd::VideoRestart>) {
            if(auto control=target->videoControl()) {
                if constexpr(std::is_same_v<T,cmd::VideoEndMode>) control->setEnd(c.value);
                else if constexpr(std::is_same_v<T,cmd::VideoPause>) control->setPaused(c.on);
                else control->restart();
            } else logWarn("Video control ignored outside video mode");
        }
        else if constexpr(std::is_same_v<T,cmd::Next>) target->next();
        else if constexpr(std::is_same_v<T,cmd::Prev>) target->prev();
        else if constexpr(std::is_same_v<T,cmd::Random>) target->random();
        else if constexpr(std::is_same_v<T,cmd::Preset>) {
            if(!target->selectPreset(c.name)) logWarn("Unknown or broken preset: "+c.name);
        } else if constexpr(std::is_same_v<T,cmd::Playlist>) {
            if(!target->selectPlaylist(c.name)) logWarn("Unknown or empty playlist: "+c.name);
        } else if constexpr(std::is_same_v<T,cmd::Lock>) {
            settings_.locked=c.on;updateSettings();
        } else if constexpr(std::is_same_v<T,cmd::Duration>) {
            settings_.duration=c.seconds;updateSettings();
        } else if constexpr(std::is_same_v<T,cmd::HardCut>) {
            settings_.hardcut=c.on;updateSettings();
        } else if constexpr(std::is_same_v<T,cmd::FluidPersistence>) {
            settings_.fluidPersistence=c.value;updateSettings();
        } else if constexpr(std::is_same_v<T,cmd::BeatSensitivity>) {
            settings_.beatSensitivity=c.value;updateSettings();
        }
    },command);
}
}
