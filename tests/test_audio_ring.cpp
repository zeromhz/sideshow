#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "audio_ring.h"

using sideshow::AudioRing;

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
