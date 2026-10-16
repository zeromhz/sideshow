#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "video_playback.h"
#include <limits>
using namespace sideshow;
TEST_CASE("Video pause lock and end policies act once at the final frame") {
    for (auto end : {VideoEnd::Loop, VideoEnd::Hold, VideoEnd::Next})
    for (bool locked : {false, true}) {
        VideoPlayback clock(end); clock.setLocked(locked); clock.beginClip();
        clock.markEof(1.0); clock.setPaused(true); clock.advance(10);
        CHECK(clock.position() == 0); CHECK(clock.takeAction() == VideoAction::None);
        clock.setPaused(false); clock.advance(.5); CHECK(clock.takeAction() == VideoAction::None);
        clock.advance(.5);
        CHECK(clock.takeAction() == (end == VideoEnd::Hold ? VideoAction::None :
            (end == VideoEnd::Loop || locked ? VideoAction::Rewind : VideoAction::Advance)));
        CHECK(clock.takeAction() == VideoAction::None);
    }
}
TEST_CASE("Changing a held video end policy applies after unpausing") {
    VideoPlayback clock(VideoEnd::Hold); clock.markEof(1); clock.advance(2);
    CHECK(clock.takeAction() == VideoAction::None);
    clock.setPaused(true); clock.setEnd(VideoEnd::Next);
    CHECK(clock.takeAction() == VideoAction::None);
    clock.setPaused(false); CHECK(clock.takeAction() == VideoAction::Advance);
    clock.setEnd(VideoEnd::Next); CHECK(clock.takeAction() == VideoAction::None);
    clock.beginClip(); clock.setLocked(true); clock.markEof(1); clock.advance(.5);
    clock.setLocked(false); clock.advance(.5); CHECK(clock.takeAction() == VideoAction::Advance);
}
TEST_CASE("Video restart retains controls and rejects invalid elapsed time") {
    VideoPlayback clock(VideoEnd::Next); clock.setPaused(true); clock.beginClip();
    CHECK(clock.paused()); CHECK(clock.end() == VideoEnd::Next);
    clock.setPaused(false); clock.advance(-1); clock.advance(std::numeric_limits<double>::quiet_NaN());
    CHECK(clock.position() == 0);
    clock.markEof(1); clock.advance(1000); CHECK(clock.takeAction() == VideoAction::Advance);
    CHECK(clock.takeAction() == VideoAction::None);
    clock.setPaused(true); clock.beginClip(); CHECK(clock.position() == 0);
    CHECK(clock.takeAction() == VideoAction::None);
}
TEST_CASE("Video presentation timestamps normalize epochs and missing values") {
    VideoTimestamps time(25);
    CHECK(time.normalize(12.5, {}).first == 0);
    CHECK(time.normalize(12.54, {}).first == Catch::Approx(.04));
    CHECK(time.normalize({}, {}).first == Catch::Approx(.08));
    CHECK(time.normalize(12.0, -1).first == Catch::Approx(.12));
    time.reset(); CHECK(time.normalize(40, .1).first == 0);
    CHECK(time.normalize({}, {}).first == Catch::Approx(.1));
    VideoTimestamps fallback(0);
    CHECK(fallback.normalize({}, {}).second == Catch::Approx(1.0/30));
    CHECK(fallback.normalize(std::numeric_limits<double>::infinity(), {}).first == Catch::Approx(1.0/30));
}
