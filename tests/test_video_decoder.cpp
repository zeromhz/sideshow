#include <catch2/catch_test_macros.hpp>
#include "support/video_tree.h"

#include <catch2/catch_approx.hpp>
#include "video_decoder.h"
#include "support/video_fixtures.h"
using namespace sideshow;
TEST_CASE("Decoder drains reordered frames and rewinds silently") {
    for (auto name : {"reordered.mp4", "bars-audio.avi"}) {
        VideoDecoder decoder(videoFixture(name), [] { return false; });
        auto first = decoder.next(); REQUIRE(first);
        CHECK(first->width == 64); CHECK(first->height == 48); CHECK(first->pts == 0);
        CHECK(first->format == VideoPixels::Yuv420);
        CHECK(first->stride == 64); CHECK(first->chromaStride == 32);
        CHECK(first->pixels.size() == 64 * 48 + 2 * 32 * 24);
        auto second=decoder.next(); REQUIRE(second); CHECK(second->pts == Catch::Approx(.2));
        auto third=decoder.next(); REQUIRE(third); CHECK(third->pts == Catch::Approx(.4));
        CHECK_FALSE(decoder.next()); CHECK_FALSE(decoder.next());
        for (int i=0;i<3;++i) { decoder.rewind(); auto again=decoder.next(); REQUIRE(again); CHECK(again->pts==0); CHECK(again->pixels==first->pixels); }
    }
}
TEST_CASE("Decoder honors variable timestamps aspect and rotation") {
    VideoDecoder variable(videoFixture("variable.mkv"), [] {return false;});
    REQUIRE(variable.next()); REQUIRE(variable.next());
    auto last=variable.next(); REQUIRE(last); CHECK(last->pts==Catch::Approx(.8));
    VideoDecoder rotated(videoFixture("rotated.mp4"), [] {return false;});
    auto frame=rotated.next(); REQUIRE(frame); CHECK(frame->pixelAspect==Catch::Approx(2));
    CHECK(frame->rotationDegrees==90);
}
TEST_CASE("Decoder rejects unsupported corrupt and cancelled sources") {
    for(auto name:{"audio-only.mov","corrupt.mp4","truncated.mp4","interlaced.mkv","hdr.mkv"}) {
        INFO(name);
        CHECK_THROWS([&] { VideoDecoder d(videoFixture(name),[]{return false;}); while(d.next()) {} }());
    }
    CHECK_THROWS(VideoDecoder(videoFixture("reordered.mp4"),[]{return true;}));
}
TEST_CASE("Video dimensions are bounded before allocating frames") {
    CHECK_THROWS(videoOutputSize(0, 48)); CHECK_THROWS(videoOutputSize(4097, 48));
    CHECK_THROWS(videoOutputSize(4096,4096));
    CHECK(videoOutputSize(3840,2160)==std::pair<int,int>{1920,1080});
    CHECK(videoOutputSize(2160,3840)==std::pair<int,int>{1080,1920});
}
TEST_CASE("Decoder refuses symlink components even when a catalog check raced") {
    VideoTree tree;tree.symlink("alias",videoFixture(""));
    CHECK_THROWS(VideoDecoder(tree.root/"alias/reordered.mp4",[]{return false;}));
}
