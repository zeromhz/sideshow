#pragma once
#include "visual_backend.h"
#include "isf_loader.h"
#include "isf_runtime.h"
namespace sideshow {
class IsfBackend final:public VisualBackend,public IsfControl {
public:
 IsfBackend(const BackendInit&,IsfLoader&,IsfContext&,IsfBudget&);~IsfBackend();
 PreparationResult preparation()override;IsfControl* isfControl()override{return this;}
 void maintenance()override;void addPcm(const float*,size_t)override;void clearAudioHistory()override;
 void render(float)override;void resize(int,int)override;void setSettings(const PlaybackSettings&)override;
 void next()override;void prev()override;void random()override;
 bool selectPreset(const std::string&)override;bool selectPlaylist(const std::string&)override;
 VisualSelection selection()const override;
 CommandResult setParameter(const cmd::IsfParam&)override;CommandResult triggerEvent(const cmd::IsfEvent&)override;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
