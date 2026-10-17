#include "video_decoder.h"
#include "support/video_fixtures.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Resolution changes are rejected before oversized decoder buffers allocate") {
    for (auto name : {"size-change.ts", "wide-change.ts", "tall-change.ts", "oversized.ts"}) {
        INFO(name);
        video_decode_tracking::oversizedAllocations = 0;
        CHECK_THROWS([&] {
            sideshow::VideoDecoder decoder(videoFixture(name), [] { return false; });
            while (decoder.next()) {}
        }());
        CHECK(video_decode_tracking::oversizedAllocations == 0);
    }
}

TEST_CASE("Decoder still accepts resolution changes within the source limits") {
    sideshow::VideoDecoder decoder(videoFixture("valid-change.ts"), [] { return false; });
    int small = 0, large = 0;
    while (auto frame = decoder.next()) {
        CHECK(frame->height == 64);
        if (frame->width == 64) ++small;
        else if (frame->width == 128) ++large;
        else FAIL("Unexpected frame dimensions");
    }
    CHECK(small == 10);
    CHECK(large == 1);
}

TEST_CASE("Header probing cannot open an unguarded decoder") {
    video_decode_tracking::probeCalls = 0;
    video_decode_tracking::probeDecoderOpens = 0;
    sideshow::VideoDecoder decoder(videoFixture("reordered.mp4"), [] { return false; });
    REQUIRE(decoder.next());
    CHECK(video_decode_tracking::probeCalls > 0);
    CHECK(video_decode_tracking::probeDecoderOpens == 0);
}
