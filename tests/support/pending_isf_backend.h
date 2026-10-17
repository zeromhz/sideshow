#pragma once
#include "pending_video_backend.h"
#include "isf_control.h"
struct PendingIsfState:PendingVideoState {int parameters=0,events=0,clears=0;size_t audioFrames=0;};
class PendingIsfBackend:public PendingVideoBackend,public sideshow::IsfControl {
 std::shared_ptr<PendingIsfState> state_;bool isf_;
public:
 PendingIsfBackend(const sideshow::BackendInit& i,std::shared_ptr<PendingIsfState> s,bool isf):PendingVideoBackend(i,s),state_(std::move(s)),isf_(isf){}
 sideshow::VideoControl* videoControl()override{return nullptr;}
 sideshow::IsfControl* isfControl()override{return isf_?this:nullptr;}
 sideshow::CommandResult setParameter(const sideshow::cmd::IsfParam&)override{++state_->parameters;return {"accepted","ISF parameter queued"};}
 sideshow::CommandResult triggerEvent(const sideshow::cmd::IsfEvent&)override{++state_->events;return {"accepted","ISF event queued"};}
 void addPcm(const float*,size_t n)override{state_->audioFrames+=n;}
 void clearAudioHistory()override{++state_->clears;}
};
