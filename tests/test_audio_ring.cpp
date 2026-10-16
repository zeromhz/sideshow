#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "audio_ring.h"
#include "audio_feed.h"

using sideshow::AudioRing;
using sideshow::AudioFeed;

TEST_CASE("AudioRing returns samples in FIFO order") {
    AudioRing ring(8);
    const float in[] = {1, 2, 3, 4};
    ring.write(in, 4);
    CHECK(ring.size() == 4);

    std::vector<float> out(8);
    CHECK(ring.read(out.data(), out.size()) == 4);
    CHECK(std::vector<float>(out.begin(), out.begin() + 4) == std::vector<float>{1, 2, 3, 4});
    CHECK(ring.size() == 0);
}

TEST_CASE("AudioRing reads at most maxCount") {
    AudioRing ring(8);
    const float in[] = {1, 2, 3, 4, 5, 6};
    ring.write(in, 6);

    std::vector<float> out(4);
    CHECK(ring.read(out.data(), 4) == 4);
    CHECK(out == std::vector<float>{1, 2, 3, 4});
    CHECK(ring.read(out.data(), 4) == 2);
    CHECK(out[0] == 5);
    CHECK(out[1] == 6);
}

TEST_CASE("AudioRing drops the oldest samples when full") {
    AudioRing ring(4);
    const float first[] = {1, 2, 3, 4};
    const float second[] = {5, 6};
    ring.write(first, 4);
    ring.write(second, 2);
    CHECK(ring.size() == 4);

    std::vector<float> out(4);
    CHECK(ring.read(out.data(), 4) == 4);
    CHECK(out == std::vector<float>{3, 4, 5, 6});
}

TEST_CASE("AudioRing keeps only the newest samples of an oversized write") {
    AudioRing ring(4);
    const float in[] = {1, 2, 3, 4, 5, 6, 7, 8};
    ring.write(in, 8);

    std::vector<float> out(4);
    CHECK(ring.read(out.data(), 4) == 4);
    CHECK(out == std::vector<float>{5, 6, 7, 8});
}

TEST_CASE("AudioFeed replaces retained audio with silence after capture disconnects") {
    AudioRing ring(8);
    AudioFeed feed(4);
    std::vector<float> retained(8);
    auto submit = [&](const float* pcm, size_t frames) {
        retained.assign(pcm, pcm + frames * 2);
    };
    const float sound[] = {1, 1, 1, 1, 1, 1, 1, 1};
    ring.write(sound, 8);
    feed.update(ring, true, 0.016f, submit);
    REQUIRE(retained == std::vector<float>(8, 1.0f));
    // A disconnect may leave captured samples waiting in the input ring.
    ring.write(sound, 8);
    feed.update(ring, false, 0.016f, submit);
    CHECK(retained == std::vector<float>(8, 0.0f));
    ring.write(sound, 8);
    feed.update(ring, true, 0.016f, submit);
    CHECK(retained == std::vector<float>(8, 1.0f));
}

TEST_CASE("AudioFeed tolerates callback gaps but clears audio when capture stalls") {
    AudioRing ring(8);
    AudioFeed feed(4);
    std::vector<float> retained;
    int submissions = 0;
    auto submit = [&](const float* pcm, size_t frames) {
        ++submissions;
        retained.assign(pcm, pcm + frames * 2);
    };
    const float sound[] = {1, 1, 1, 1, 1, 1, 1, 1};
    ring.write(sound, 8);
    feed.update(ring, true, 0.016f, submit);
    feed.update(ring, true, 0.064f, submit);
    CHECK(submissions == 1);  // A 512-frame callback at 8 kHz can take 64 ms.
    feed.update(ring, true, 0.250f, submit);
    CHECK(retained == std::vector<float>(8, 0.0f));
}

TEST_CASE("AudioFeed defers samples arriving during a multi-block update to the next frame") {
    AudioRing ring(8);
    AudioFeed feed(2);
    const float initial[] = {1, 1, 1, 1, 1, 1, 1, 1};
    const float arriving[] = {2, 2, 2, 2};
    ring.write(initial, 8);
    std::vector<float> delivered;
    int calls = 0;
    feed.update(ring, true, 0.016f, [&](const float* pcm, size_t frames) {
        delivered.insert(delivered.end(), pcm, pcm + frames * 2);
        // A finite producer also makes an accidental unbounded drain testable.
        if (++calls < 8) ring.write(arriving, 4);
    });
    CHECK(delivered == std::vector<float>(8, 1.0f));
    CHECK(ring.size() == 8);
    delivered.clear();
    feed.update(ring, true, 0.016f, [&](const float* pcm, size_t frames) {
        delivered.insert(delivered.end(), pcm, pcm + frames * 2);
    });
    CHECK(delivered == std::vector<float>(8, 2.0f));
    CHECK(ring.size() == 0);
}

TEST_CASE("AudioFeed clears projectM's full retained waveform across submission blocks") {
    AudioRing input(1152);
    // projectM 4.1.7 reports a 480-frame submission limit but retains 576 frames.
    AudioFeed feed(480);
    AudioRing retained(1152);
    std::vector<float> sound(1152, 1.0f);
    retained.write(sound.data(), sound.size());
    feed.update(input, false, 0.016f, [&](const float* pcm, size_t frames) {
        CHECK(frames <= 480);
        retained.write(pcm, frames * 2);
    });
    std::vector<float> waveform(1152);
    REQUIRE(retained.read(waveform.data(), waveform.size()) == 1152);
    CHECK(std::all_of(waveform.begin(), waveform.end(), [](float sample) { return sample == 0.0f; }));
}
