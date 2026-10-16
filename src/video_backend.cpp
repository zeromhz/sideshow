#include "video_backend.h"
#include "log.h"
#include <algorithm>
namespace sideshow {
VideoBackend::VideoBackend(const BackendInit& init,VideoWorker& worker)
    :worker_(worker),presenter_(init.width,init.height),clock_(init.videoEnd),
     session_(worker.acquireSession()),startupPreset_(init.selection.preset) {clock_.setLocked(init.playback.locked);}
VideoBackend::~VideoBackend() {worker_.cancel(session_);}
void VideoBackend::request(std::vector<std::string> ids) {
    attempts_=std::move(ids);attempt_=0;tryNext();
}
void VideoBackend::tryNext() {
    if(attempt_>=attempts_.size()) {
        loading_=false;
        if(!activeGeneration_) preparation_={PreparationState::Failed,"No usable video clips"};
        return;
    }
    loading_=true;worker_.open({session_,++generation_,attempts_[attempt_++]});
}
void VideoBackend::failed(const std::string& error) {logWarn(error);tryNext();}
bool VideoBackend::selectPreset(const std::string& id) {
    if(!validVideoId(id)) return false;
    if(!catalog_) {deferredPreset_=id;deferredFolder_.reset();return true;}
    if(!catalog_->find(id)) return false;
    request({id});return true;
}
bool VideoBackend::selectPlaylist(const std::string& folder) {
    if(folder!="." && !validVideoId(folder)) return false;
    if(!catalog_) {deferredFolder_=folder;deferredPreset_.reset();return true;}
    auto ids=catalog_->inFolder(folder);if(ids.empty()) return false;request(std::move(ids));return true;
}
void VideoBackend::navigate(int direction,bool randomize) {
    if(!catalog_) return;
    auto ids=catalog_->inFolder(selection_.playlist);
    if(ids.empty()) return;
    auto it=std::find(ids.begin(),ids.end(),selection_.preset);
    size_t index=it==ids.end()?0:size_t(it-ids.begin());
    if(randomize && ids.size()>1) index=(index+1+std::uniform_int_distribution<size_t>(0,ids.size()-2)(rng_))%ids.size();
    else index=(index+ids.size()+direction)%ids.size();
    if(direction<0) {std::reverse(ids.begin(),ids.end());index=ids.size()-1-index;}
    std::rotate(ids.begin(),ids.begin()+index,ids.end());request(std::move(ids));
}
void VideoBackend::next() {navigate(1);}
void VideoBackend::prev() {navigate(-1);}
void VideoBackend::random() {navigate(1,true);}
void VideoBackend::setPaused(bool value) {clock_.setPaused(value);worker_.playback(session_,clock_.position(),value);}
void VideoBackend::restart() {
    if(!activeGeneration_) return;
    attempts_.clear();attempt_=0;loading_=true;worker_.rewind(session_,++generation_);
}
void VideoBackend::maintenance() {
    for(int count=0;count<8;++count) {
        auto event=worker_.poll(session_);if(!event) break;
        if(event->kind==VideoEventKind::Catalog) {
            catalog_=std::move(event->catalog);
            for(const auto& warning:catalog_->warnings()) logWarn(warning);
            if(deferredPreset_) {
                auto id=*deferredPreset_;deferredPreset_.reset();request({id});
            } else if(deferredFolder_) {
                auto folder=*deferredFolder_;deferredFolder_.reset();request(catalog_->inFolder(folder));
            } else {
                std::vector<std::string> ids;
                if(!startupPreset_.empty()) ids.push_back(startupPreset_);
                for(const auto& clip:catalog_->clips()) if(clip.id!=startupPreset_) ids.push_back(clip.id);
                request(std::move(ids));
            }
        } else if(event->kind==VideoEventKind::Frame && event->frame) {
            if(event->generation!=generation_ && event->generation!=activeGeneration_) continue;
            if(loading_ && event->generation!=generation_) continue;
            try {presenter_.upload(*event->frame);}
            catch(const std::exception& e) {
                if(loading_) {worker_.reject(session_,event->generation);failed(e.what());continue;} throw;
            }
            if(loading_) {
                activeGeneration_=event->generation;clock_.beginClip();loading_=false;attempts_.clear();
                if(auto clip=catalog_->find(event->frame->clipId)) selection_={clip->folder,clip->id};
                worker_.commit(session_,activeGeneration_);preparation_={PreparationState::Ready,{}};
                worker_.playback(session_,0,clock_.paused());
            }
        } else if(event->kind==VideoEventKind::Eof && event->generation==activeGeneration_ && !loading_)
            clock_.markEof(event->endSeconds);
        else if(event->kind==VideoEventKind::Failed) {
            if(loading_ && event->generation==generation_) failed(event->error);
            else if(event->generation==activeGeneration_) logWarn(event->error);
        }
    }
}
void VideoBackend::render(float dt) {
    if(!loading_) clock_.advance(dt);
    worker_.playback(session_,clock_.position(),clock_.paused());
    if(!loading_) switch(clock_.takeAction()) {
        case VideoAction::Rewind: restart();break;
        case VideoAction::Advance: next();break;
        case VideoAction::None:break;
    }
    presenter_.render();
}
}
