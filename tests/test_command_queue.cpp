#include <catch2/catch_test_macros.hpp>

#include <thread>
#include <vector>

#include "command.h"

using namespace sideshow;

TEST_CASE("CommandQueue pops nothing when empty") {
    CommandQueue queue;
    CHECK_FALSE(queue.pop().has_value());
}

TEST_CASE("CommandQueue preserves navigation order and payloads") {
    CommandQueue queue;
    queue.push(cmd::Next{});
    queue.push(cmd::Preset{"waves"});
    queue.push(cmd::Playlist{"mellow"});

    auto first = queue.pop();
    REQUIRE(first);
    CHECK(std::holds_alternative<cmd::Next>(*first));

    auto second = queue.pop();
    REQUIRE(second);
    REQUIRE(std::holds_alternative<cmd::Preset>(*second));
    CHECK(std::get<cmd::Preset>(*second).name == "waves");

    auto third = queue.pop();
    REQUIRE(third);
    REQUIRE(std::holds_alternative<cmd::Playlist>(*third));
    CHECK(std::get<cmd::Playlist>(*third).name == "mellow");

    CHECK_FALSE(queue.pop().has_value());
}

TEST_CASE("CommandQueue accepts pushes from another thread") {
    CommandQueue queue;
    std::thread producer([&] {
        for (int i = 0; i < 32; ++i) queue.push(cmd::Random{});
    });
    producer.join();
    int count = 0;
    while (queue.pop()) ++count;
    CHECK(count == 32);
}

TEST_CASE("CommandQueue bounds navigation backlog during overload") {
    CommandQueue queue;
    for (int i = 0; i < 10000; ++i) queue.push(cmd::Next{});
    int count = 0;
    while (queue.pop()) ++count;
    CHECK(count > 0);
    CHECK(count <= 128);
}

TEST_CASE("CommandQueue coalesces pending master changes to the latest value") {
    CommandQueue queue;
    queue.push(cmd::Next{});
    for (int i = 0; i < 10000; ++i) queue.push(cmd::Master{static_cast<float>(i)});
    int masters = 0;
    while (auto command = queue.pop()) {
        if (const auto* master = std::get_if<cmd::Master>(&*command)) {
            ++masters;
            CHECK(master->value == 9999.0f);
        }
    }
    CHECK(masters == 1);
}

TEST_CASE("CommandQueue delivers latest blackout before an overloaded backlog") {
    CommandQueue queue;
    for (int i = 0; i < 10000; ++i) queue.push(cmd::Next{});
    queue.push(cmd::Blackout{false});
    queue.push(cmd::Blackout{true});
    auto command = queue.pop();
    REQUIRE(command);
    REQUIRE(std::holds_alternative<cmd::Blackout>(*command));
    CHECK(std::get<cmd::Blackout>(*command).on);
    while (auto remaining = queue.pop()) {
        CHECK_FALSE(std::holds_alternative<cmd::Blackout>(*remaining));
    }
}

TEST_CASE("CommandQueue yields a frame even while a producer replenishes work") {
    CommandQueue queue;
    queue.push(cmd::Next{});
    int processed = 0;
    queue.processFrame([&](const Command&) {
        ++processed;
        if (processed < 32) queue.push(cmd::Next{});
    });
    CHECK(processed > 0);
    CHECK(processed <= 8);
    CHECK(queue.pop().has_value());
}

TEST_CASE("CommandQueue preserves master control even when navigation is full") {
    CommandQueue queue;
    for (int i = 0; i < 10000; ++i) queue.push(cmd::Next{});
    queue.push(cmd::Master{0.25f});
    auto command = queue.pop();
    REQUIRE(command);
    REQUIRE(std::holds_alternative<cmd::Master>(*command));
    CHECK(std::get<cmd::Master>(*command).value == 0.25f);
}

TEST_CASE("CommandQueue yields after a slow preset operation exhausts the time budget") {
    CommandQueue queue;
    queue.push(cmd::Next{});
    queue.push(cmd::Next{});
    int processed = 0;
    queue.processFrame([&](const Command&) {
        ++processed;
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    });
    CHECK(processed == 1);
    CHECK(queue.pop().has_value());
}

TEST_CASE("Acknowledgements follow application and retain applied values") {
    CommandQueue queue;
    std::vector<CommandResult> replies;
    queue.push(cmd::FluidPersistence{4},[&](const CommandResult& r){replies.push_back(r);});
    CHECK(replies.empty());
    queue.processAcknowledgedFrame([](const Command& c){
        return CommandResult{"applied",std::to_string(std::get<cmd::FluidPersistence>(c).value)};
    });
    REQUIRE(replies.size()==1);
    CHECK(replies[0].status=="applied"); CHECK(replies[0].detail=="4.000000");
}
TEST_CASE("Acknowledgements report superseded output controls and rejected backlog") {
    CommandQueue queue;std::vector<std::string> replies;
    auto reply=[&](const CommandResult& r){replies.push_back(r.status);};
    queue.push(cmd::Master{.1f},reply);
    queue.push(cmd::Master{.8f},reply);
    REQUIRE(replies==std::vector<std::string>{"superseded"});
    queue.processAcknowledgedFrame([](const Command& c){
        CHECK(std::get<cmd::Master>(c).value==.8f); return CommandResult{"applied","0.8"};
    });
    REQUIRE(replies==std::vector<std::string>{"superseded","applied"});
    for(int i=0;i<128;++i)queue.push(cmd::Next{});
    queue.push(cmd::Next{},reply);
    CHECK(replies.back()=="rejected");
}
TEST_CASE("Acknowledgement callbacks execute outside queue mutex") {
    CommandQueue queue;
    queue.push(cmd::Blackout{false},[&](const CommandResult&){queue.push(cmd::Next{});});
    queue.push(cmd::Blackout{true});
    CHECK(std::holds_alternative<cmd::Blackout>(*queue.pop()));
    CHECK(std::holds_alternative<cmd::Next>(*queue.pop()));
}
