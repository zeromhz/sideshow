#include "video_worker.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <cmath>
namespace sideshow {
struct VideoWorker::Impl {
    std::filesystem::path root;
    VideoDecoderFactory factory;
    std::mutex mutex;
    std::condition_variable wake;
    std::atomic<bool> stopping{false};
    std::atomic<uint64_t> revision{0};
    // The decoder callback runs on this worker only; a control change interrupts its operation.
    uint64_t executingRevision=0;
    uint64_t serial=0, session=0, workerSession=0, committed=0, rewindGeneration=0, activeGeneration=0, rejected=0;
    std::optional<VideoOpenRequest> requested;
    double position=0;
    bool paused=false, preparing=false, looping=false;
    std::deque<VideoEvent> frames;
    std::optional<VideoEvent> catalogEvent,errorEvent,eofEvent,releaseEvent;
    std::shared_ptr<const VideoCatalog> catalog;
    struct Context {
        std::unique_ptr<VideoSource> source;
        uint64_t generation=0;
        std::string id;
        double end=0;
        // Seamless looping: each pass after the first is shifted by the timeline length so far,
        // so frame times keep increasing across the loop point.
        double offset=0;
        bool done=false;
    } current,candidate;
    std::shared_ptr<std::atomic<int>> retained=std::make_shared<std::atomic<int>>(0);
    std::shared_ptr<void> credit() {
        ++*retained;
        return std::shared_ptr<void>(retained.get(),[count=retained](void*){--*count;});
    }
    std::thread thread;
    Impl(std::filesystem::path path,VideoDecoderFactory f):root(std::move(path)),factory(std::move(f)) {
        if(!factory) factory=[](const auto& path,auto cancel){return std::make_unique<VideoDecoder>(path,std::move(cancel));};
        thread=std::thread([this]{run();});
    }
    ~Impl() { stopping=true; ++revision; wake.notify_all();thread.join(); }
    bool cancelled() const {return stopping || revision.load()!=executingRevision;}
    VideoEvent event(VideoEventKind kind,uint64_t generation=0) {return {kind,workerSession,generation,{},{},{},0};}
    void run() noexcept {
        try { loop(); }
        catch(const std::exception& e) {
            std::lock_guard<std::mutex> lock(mutex);
            errorEvent=event(VideoEventKind::Failed);errorEvent->error=e.what();
        } catch(...) {
            std::lock_guard<std::mutex> lock(mutex);
            errorEvent=event(VideoEventKind::Failed);errorEvent->error="Unexpected video worker failure";
        }
        // All decoder destruction is on the worker, including shutdown.
        current={};candidate={};
    }
    void loop() {
        std::unique_lock<std::mutex> lock(mutex);
        while(!stopping) {
            executingRevision=revision.load();
            if(workerSession!=session) {
                auto old=workerSession;workerSession=session;
                activeGeneration=0;
                frames.clear();catalogEvent.reset();errorEvent.reset();eofEvent.reset();
                lock.unlock();current={};candidate={};lock.lock();
                if(old) releaseEvent=VideoEvent{VideoEventKind::Released,old,0,{},{},{},0};
                if(!workerSession) continue;
                if(!catalog) {
                    lock.unlock();
                    auto scanned=std::make_shared<VideoCatalog>(VideoCatalog::scan(root,[this]{return cancelled();}));
                    lock.lock();if(cancelled()) {workerSession=0;continue;} catalog=std::move(scanned);
                }
                catalogEvent=event(VideoEventKind::Catalog);catalogEvent->catalog=catalog;
            }
            if(!session) {wake.wait(lock);continue;}
            if(candidate.source && committed==candidate.generation) {
                lock.unlock();current=std::move(candidate);candidate={};lock.lock();
                activeGeneration=current.generation;committed=0;
            }
            // The render thread notifies on every frame (playback()) and whenever it takes a
            // frame (poll()), so these timed waits are only a fallback; a 5 ms timeout woke the
            // worker about 200 times a second even while paused.
            if((requested || rewindGeneration) && retained->load()>=3) {
                wake.wait_for(lock,std::chrono::milliseconds(50));continue;
            }
            if(requested || rewindGeneration) {
                auto request=requested;requested.reset();auto rewind=rewindGeneration;rewindGeneration=0;
                frames.clear();errorEvent.reset();committed=0;
                const uint64_t generation=request?request->generation:rewind;
                lock.unlock();
                try {
                    candidate={};
                    if(request) {
                        auto file=catalog->resolveForOpen(request->clipId);
                        if(!file) throw std::runtime_error("Video clip is missing or unsafe: "+request->clipId);
                        candidate.source=factory(*file,[this]{return cancelled();});
                        candidate.id=request->clipId;
                    } else {
                        if(!current.source) throw std::runtime_error("No video to rewind");
                        candidate=std::move(current);current={};candidate.source->rewind();candidate.done=false;candidate.offset=0;
                    }
                    candidate.generation=generation;
                    auto budget=credit();
                    auto frame=candidate.source->next();
                    if(!frame) throw std::runtime_error("Video has no decoded frames");
                    frame->bufferCredit=std::move(budget);
                    candidate.end=frame->pts+frame->duration;
                    frame->generation=generation;frame->clipId=candidate.id;
                    lock.lock();
                    if(!cancelled()) {auto output=event(VideoEventKind::Frame,generation);output.frame=std::move(frame);frames.push_back(std::move(output));}
                    else {lock.unlock();candidate={};lock.lock();}
                } catch(const std::exception& e) {
                    candidate={};lock.lock();
                    if(!cancelled()) {preparing=false;errorEvent=event(VideoEventKind::Failed,generation);errorEvent->error=e.what();}
                }
                continue;
            }
            if(candidate.source && rejected==candidate.generation) {
                lock.unlock();candidate={};lock.lock();rejected=0;
            }
            if(candidate.source) {
                if(committed==candidate.generation) {
                    lock.unlock();current=std::move(candidate);candidate={};lock.lock();committed=0;
                } else {wake.wait(lock);continue;}
            }
            if(!current.source || current.done || paused || retained->load()>=3 || current.end>position+.25) {
                wake.wait_for(lock,std::chrono::milliseconds(50));continue;
            }
            const auto generation=current.generation;
            lock.unlock();
            try {
                auto budget=credit();
                auto frame=current.source->next();
                lock.lock();
                if(cancelled()) continue;
                if(frame) {
                    frame->bufferCredit=std::move(budget);frame->pts+=current.offset;
                    current.end=frame->pts+frame->duration;frame->generation=generation;frame->clipId=current.id;
                    auto output=event(VideoEventKind::Frame,generation);output.frame=std::move(frame);frames.push_back(std::move(output));
                } else if(looping && current.end>current.offset) {
                    // Loop without a round trip through the render thread: rewind here and keep
                    // decoding, shifted by this pass's length. The next pass's first frames are
                    // decoded ahead like any others, so the loop point does not stall. (A pass
                    // that added no time, e.g. zero-length frames, falls back to EOF.)
                    const double passEnd=current.end;
                    lock.unlock();
                    try {current.source->rewind();}
                    catch(const std::exception&) {
                        // Report a plain EOF: the backend's own loop path then rewinds, and moves
                        // on to the next clip if that fails too.
                        lock.lock();
                        if(!cancelled()) {current.done=true;eofEvent=event(VideoEventKind::Eof,generation);eofEvent->endSeconds=current.end;}
                        continue;
                    }
                    lock.lock();
                    current.offset=passEnd;
                } else {current.done=true;eofEvent=event(VideoEventKind::Eof,generation);eofEvent->endSeconds=current.end;}
            } catch(const std::exception& e) {
                lock.lock();
                if(!cancelled()) {current.done=true;errorEvent=event(VideoEventKind::Failed,generation);errorEvent->error=e.what();}
            }
        }
    }
};
VideoWorker::VideoWorker(std::filesystem::path root,VideoDecoderFactory factory):impl_(std::make_unique<Impl>(std::move(root),std::move(factory))) {}
VideoWorker::~VideoWorker()=default;
uint64_t VideoWorker::acquireSession() {
    auto& p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);
    p.session=++p.serial;p.requested.reset();p.rewindGeneration=0;p.committed=0;p.position=0;p.paused=false;
    // Generation numbers restart in each backend session.
    p.activeGeneration=0;p.rejected=0;p.preparing=false;
    ++p.revision;p.wake.notify_all();return p.session;
}
void VideoWorker::open(VideoOpenRequest request) {
    auto& p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);
    if(!request.session || request.session!=p.session) return;
    p.requested=std::move(request);p.rewindGeneration=0;p.frames.clear();p.errorEvent.reset();p.preparing=true;++p.revision;p.wake.notify_all();
}
void VideoWorker::commit(uint64_t session,uint64_t generation) {
    auto& p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);
    if(session!=p.session) return;p.committed=generation;p.preparing=false;p.eofEvent.reset();p.wake.notify_all();
}
void VideoWorker::reject(uint64_t session,uint64_t generation) {
    auto& p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);
    if(session!=p.session) return;p.rejected=generation;p.preparing=false;p.wake.notify_all();
}
void VideoWorker::cancel(uint64_t session) {
    auto& p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);
    if(session!=p.session) return;p.session=0;p.requested.reset();p.rewindGeneration=0;++p.revision;p.wake.notify_all();
}
void VideoWorker::rewind(uint64_t session,uint64_t generation) {
    auto& p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);
    if(session!=p.session) return;p.rewindGeneration=generation;p.requested.reset();p.frames.clear();p.errorEvent.reset();p.preparing=true;++p.revision;p.wake.notify_all();
}
void VideoWorker::playback(uint64_t session,double position,bool paused,bool looping) {
    auto& p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);
    if(session!=p.session || !std::isfinite(position)) return;p.position=position;p.paused=paused;p.looping=looping;p.wake.notify_all();
}
std::optional<VideoEvent> VideoWorker::poll(uint64_t session) {
    auto& p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);
    auto take=[&](std::optional<VideoEvent>& slot)->std::optional<VideoEvent> {
        if(!slot || slot->session!=session) return {};auto result=std::move(slot);slot.reset();return result;
    };
    if(auto e=take(p.releaseEvent)) return e;
    if(session!=p.session) return {};
    if(auto e=take(p.catalogEvent)) return e;
    if(auto e=take(p.errorEvent)) return e;
    if(!p.frames.empty() && (p.frames.front().generation!=p.activeGeneration ||
        (!p.paused && p.frames.front().frame->pts<=p.position))) {
        auto result=std::move(p.frames.front());p.frames.pop_front();p.wake.notify_all();return result;
    }
    if(p.frames.empty() && !p.preparing) if(auto e=take(p.eofEvent)) return e;
    return {};
}
}
