#include <catch2/catch_test_macros.hpp>

#include <thread>
#include <vector>

#include "command.h"

using namespace sideshow;

TEST_CASE("CommandQueue pops nothing when empty") {
    CommandQueue queue;
    CHECK_FALSE(queue.pop().has_value());
}

TEST_CASE("CommandQueue is FIFO and preserves payloads") {
    CommandQueue queue;
    queue.push(cmd::Next{});
    queue.push(cmd::Master{0.25f});
    queue.push(cmd::Playlist{"mellow"});

    auto first = queue.pop();
    REQUIRE(first);
    CHECK(std::holds_alternative<cmd::Next>(*first));

    auto second = queue.pop();
    REQUIRE(second);
    REQUIRE(std::holds_alternative<cmd::Master>(*second));
    CHECK(std::get<cmd::Master>(*second).value == 0.25f);

    auto third = queue.pop();
    REQUIRE(third);
    REQUIRE(std::holds_alternative<cmd::Playlist>(*third));
    CHECK(std::get<cmd::Playlist>(*third).name == "mellow");

    CHECK_FALSE(queue.pop().has_value());
}

TEST_CASE("CommandQueue accepts pushes from another thread") {
    CommandQueue queue;
    std::thread producer([&] {
        for (int i = 0; i < 1000; ++i) queue.push(cmd::Random{});
    });
    producer.join();
    int count = 0;
    while (queue.pop()) ++count;
    CHECK(count == 1000);
}
