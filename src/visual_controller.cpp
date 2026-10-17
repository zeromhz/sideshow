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
CommandResult VisualController::apply(const Command& command) {
    auto* target=pending_ ? pending_.get() : active_.get();
    return std::visit([&](const auto& c) -> CommandResult {
        using T=std::decay_t<decltype(c)>;
        if constexpr(std::is_same_v<T,cmd::Mode>) {
            if(!setMode(c.value)) return {"rejected","Mode initialization failed"};
            const auto name=std::string(visualModeName(c.value));
            if(pending_) return {"accepted","Mode loading: "+name};
            return {"applied","mode="+name};
        } else if constexpr(std::is_same_v<T,cmd::IsfParam> || std::is_same_v<T,cmd::IsfEvent>) {
            if(auto control=target->isfControl()) {
                if constexpr(std::is_same_v<T,cmd::IsfParam>) return control->setParameter(c);
                else return control->triggerEvent(c);
            }
            return {"rejected","ISF control requires ISF mode"};
        } else if constexpr(std::is_same_v<T,cmd::VideoEndMode> || std::is_same_v<T,cmd::VideoPause> || std::is_same_v<T,cmd::VideoRestart>) {
            if(auto control=target->videoControl()) {
                if constexpr(std::is_same_v<T,cmd::VideoEndMode>) {
                    control->setEnd(c.value);
                    return {"applied","video_end="+std::string(videoEndName(control->end()))};
                } else if constexpr(std::is_same_v<T,cmd::VideoPause>) {
                    control->setPaused(c.on);
                    return {"accepted",std::string("Video pause request accepted: ")+(c.on?"on":"off")};
                } else {
                    if(target->preparation().state!=PreparationState::Ready)
                        return {"rejected","Video restart requires a loaded clip; retry after loading"};
                    control->restart();
                    return {"accepted","Video restart request accepted"};
                }
            }
            return {"rejected","Video control requires video mode"};
        } else if constexpr(std::is_same_v<T,cmd::Next> || std::is_same_v<T,cmd::Prev> || std::is_same_v<T,cmd::Random>) {
            // These backends drop navigation before their initial catalog is ready.
            // Readiness also guarantees a current selection for relative navigation.
            if((target->videoControl() || target->isfControl()) &&
               target->preparation().state!=PreparationState::Ready)
                return {"rejected","Navigation requires a loaded visual; retry after loading"};
            std::string detail;
            if constexpr(std::is_same_v<T,cmd::Next>) {
                target->next();detail="Next preset request accepted";
            } else if constexpr(std::is_same_v<T,cmd::Prev>) {
                target->prev();detail="Previous preset request accepted";
            } else {
                target->random();detail="Random preset request accepted";
            }
            if(!pending_ && mode_==VisualMode::Fluid)
                return {"applied","preset="+target->selection().preset};
            return {"accepted",std::move(detail)};
        } else if constexpr(std::is_same_v<T,cmd::Preset>) {
            if(!target->selectPreset(c.name)) return {"rejected","Unknown or broken preset: "+c.name};
            if(!pending_ && mode_==VisualMode::Fluid)
                return {"applied","preset="+target->selection().preset};
            return {"accepted","Preset request accepted: "+c.name};
        } else if constexpr(std::is_same_v<T,cmd::Playlist>) {
            if(!target->selectPlaylist(c.name)) return {"rejected","Unknown or empty playlist: "+c.name};
            if(!pending_ && mode_==VisualMode::Fluid)
                return {"applied","playlist="+target->selection().playlist};
            return {"accepted","Playlist request accepted: "+c.name};
        } else if constexpr(std::is_same_v<T,cmd::Lock>) {
            settings_.locked=c.on;updateSettings();
            return {"applied",std::string("lock=")+(settings_.locked?"on":"off")};
        } else if constexpr(std::is_same_v<T,cmd::Duration>) {
            settings_.duration=c.seconds;updateSettings();
            return {"applied","duration="+std::to_string(settings_.duration)};
        } else if constexpr(std::is_same_v<T,cmd::HardCut>) {
            settings_.hardcut=c.on;updateSettings();
            return {"applied",std::string("hardcut=")+(settings_.hardcut?"on":"off")};
        } else if constexpr(std::is_same_v<T,cmd::FluidPersistence>) {
            settings_.fluidPersistence=c.value;updateSettings();
            return {"applied","fluid_persistence="+std::to_string(settings_.fluidPersistence)};
        } else if constexpr(std::is_same_v<T,cmd::BeatSensitivity>) {
            settings_.beatSensitivity=c.value;updateSettings();
            return {"applied","beat_sensitivity="+std::to_string(settings_.beatSensitivity)};
        }
        return {"rejected","Command is not a visual control"};
    },command);
}
}
