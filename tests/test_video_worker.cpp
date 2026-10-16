#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "video_worker.h"
#include "support/video_fixtures.h"
#include <chrono>
#include <thread>
using namespace sideshow;
namespace {
VideoEvent waitEvent(VideoWorker& worker,uint64_t session,VideoEventKind kind) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(std::chrono::steady_clock::now()<deadline) {
        if(auto event=worker.poll(session);event && event->kind==kind) return std::move(*event);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("Timed out waiting for video event");
}
}
TEST_CASE("Video worker prepares commits rewinds and releases sessions") {
    VideoWorker worker(videoFixture("")); auto session=worker.acquireSession();
    auto catalog=waitEvent(worker,session,VideoEventKind::Catalog); REQUIRE(catalog.catalog);
    worker.open({session,1,"reordered.mp4"});
    auto first=waitEvent(worker,session,VideoEventKind::Frame);
    REQUIRE(first.frame);CHECK(first.frame->pts==0);CHECK(first.generation==1);
    worker.commit(session,1);worker.playback(session,10,false);
    auto end=waitEvent(worker,session,VideoEventKind::Eof);CHECK(end.endSeconds==Catch::Approx(.6));
    worker.rewind(session,2);auto again=waitEvent(worker,session,VideoEventKind::Frame);
    REQUIRE(again.frame);CHECK(again.generation==2);CHECK(again.frame->pts==0);
    worker.commit(session,2);worker.cancel(session);
    CHECK(waitEvent(worker,session,VideoEventKind::Released).session==session);
    worker.open({session,3,"reordered.mp4"}); CHECK_FALSE(worker.poll(session));
}
TEST_CASE("Video worker coalesces replacements and preserves current after a bad candidate") {
    VideoWorker worker(videoFixture(""));auto session=worker.acquireSession();
    waitEvent(worker,session,VideoEventKind::Catalog);
    for(uint64_t generation=1;generation<=100;++generation) worker.open({session,generation,"reordered.mp4"});
    auto first=waitEvent(worker,session,VideoEventKind::Frame);CHECK(first.generation==100);
    worker.commit(session,100);worker.open({session,101,"corrupt.mp4"});
    CHECK(waitEvent(worker,session,VideoEventKind::Failed).generation==101);
    worker.playback(session,10,false);
    CHECK(waitEvent(worker,session,VideoEventKind::Eof).generation==100);
}
TEST_CASE("Video worker shutdown cancels blocked decoding cooperatively") {
    struct Blocking : VideoSource {
        std::function<bool()> cancelled;
        explicit Blocking(std::function<bool()> f):cancelled(std::move(f)){}
        std::optional<VideoFrame> next() override {
            while(!cancelled()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            throw std::runtime_error("cancelled");
        }
        void rewind() override {}
    };
    auto start=std::chrono::steady_clock::now();
    {
        VideoWorker worker(videoFixture(""),[](const auto&,auto cancel){return std::make_unique<Blocking>(cancel);});
        auto session=worker.acquireSession(); waitEvent(worker,session,VideoEventKind::Catalog);
        worker.open({session,1,"reordered.mp4"});
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    CHECK(std::chrono::steady_clock::now()-start<std::chrono::seconds(1));
}
TEST_CASE("Video worker counts polled frames against the three frame budget") {
    struct Endless : VideoSource {
        std::optional<VideoFrame> next() override { VideoFrame f; f.width=f.height=1;f.stride=4;f.rgba={1,2,3,255};return f; }
        void rewind() override {}
    };
    VideoWorker worker(videoFixture(""),[](const auto&,auto){return std::make_unique<Endless>();});
    auto session=worker.acquireSession();waitEvent(worker,session,VideoEventKind::Catalog);
    worker.open({session,1,"reordered.mp4"});
    std::vector<VideoEvent> held;held.push_back(waitEvent(worker,session,VideoEventKind::Frame));
    worker.commit(session,1);worker.playback(session,100,false);
    held.push_back(waitEvent(worker,session,VideoEventKind::Frame));
    held.push_back(waitEvent(worker,session,VideoEventKind::Frame));
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CHECK_FALSE(worker.poll(session));
    held.clear();CHECK(waitEvent(worker,session,VideoEventKind::Frame).frame.has_value());
}
