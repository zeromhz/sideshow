#pragma once
#include "video_catalog.h"
#include "video_decoder.h"
namespace sideshow {
enum class VideoEventKind { Catalog, Frame, Eof, Failed, Released };
struct VideoEvent {
    VideoEventKind kind;
    uint64_t session=0, generation=0;
    std::optional<VideoFrame> frame;
    std::shared_ptr<const VideoCatalog> catalog;
    std::string error;
    double endSeconds=0;
};
struct VideoOpenRequest { uint64_t session=0, generation=0; std::string clipId; };
class VideoWorker {
public:
    explicit VideoWorker(std::filesystem::path root, VideoDecoderFactory factory={});
    ~VideoWorker();
    uint64_t acquireSession();
    void open(VideoOpenRequest request);
    void commit(uint64_t session,uint64_t generation);
    void cancel(uint64_t session);
    void reject(uint64_t session,uint64_t generation);
    void rewind(uint64_t session,uint64_t newGeneration);
    // looping: at the end of the clip, rewind and continue on the same timeline instead of
    // reporting EOF (the Loop end policy).
    void playback(uint64_t session,double position,bool paused,bool looping=false);
    std::optional<VideoEvent> poll(uint64_t session);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
