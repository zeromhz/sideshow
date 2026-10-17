#include "isf_playback.h"
#include "support/isf_tree.h"
#include <catch2/catch_test_macros.hpp>
using namespace sideshow;

static std::shared_ptr<const IsfCatalog> catalog(IsfTree& tree) {
    tree.file("a.fs", "/*{}*/");
    tree.file("b.fs", "/*{}*/");
    tree.file("folder/c.fs", "/*{}*/");
    tree.file("folder/deeper/d.fs", "/*{}*/");
    return std::make_shared<IsfCatalog>(IsfCatalog::scan(tree.root, {}));
}

TEST_CASE("ISF playback resolves startup fallback and stops exhausted retries", "[isf]") {
    IsfTree tree;
    IsfPlayback playback({".", "a.fs"}, PlaybackSettings{}, 1);
    REQUIRE_FALSE(playback.takeRequest());
    playback.setCatalog(catalog(tree));
    auto first = playback.takeRequest();
    REQUIRE(first->id == "a.fs");
    playback.failed(first->generation, "broken a");
    auto second = playback.takeRequest();
    REQUIRE(second->id == "b.fs");
    REQUIRE_FALSE(playback.accepts(first->generation));
    playback.failed(second->generation, "broken b");
    REQUIRE_FALSE(playback.pending());
    REQUIRE(playback.error() == "broken b");
    playback.presented(100);
    REQUIRE_FALSE(playback.takeRequest());
}

TEST_CASE("ISF playback rejects superseded results and controls after invalid selection", "[isf]") {
    IsfTree tree;
    IsfPlayback playback({".", "a.fs"}, PlaybackSettings{}, 1);
    playback.setCatalog(catalog(tree));
    auto first = playback.takeRequest();
    REQUIRE(playback.commit(first->generation, first->id));
    REQUIRE(playback.route(cmd::IsfParam{"level", {.2f}}) == IsfControlRoute::Apply);
    REQUIRE(playback.selectPreset("b.fs"));
    auto pending = playback.takeRequest();
    REQUIRE(playback.route(cmd::IsfEvent{"reset"}) == IsfControlRoute::Queued);
    REQUIRE_FALSE(playback.selectPreset("missing.fs"));
    REQUIRE(playback.takeRequest()->id.empty());
    REQUIRE_FALSE(playback.commit(pending->generation, pending->id));
    REQUIRE(playback.controls().empty());
    REQUIRE(playback.selection().preset == "a.fs");
    REQUIRE(playback.route(cmd::IsfParam{"level", {.8f}}) == IsfControlRoute::Rejected);
    REQUIRE(playback.route(cmd::IsfEvent{"reset"}) == IsfControlRoute::Rejected);
    REQUIRE_FALSE(playback.selectPlaylist("missing"));
    REQUIRE(playback.route(cmd::IsfEvent{"reset"}) == IsfControlRoute::Rejected);
    REQUIRE(playback.selectPreset("a.fs"));
    auto reload = playback.takeRequest();
    REQUIRE(playback.commit(reload->generation, reload->id));
    REQUIRE(playback.route(cmd::IsfEvent{"reset"}) == IsfControlRoute::Apply);
}

TEST_CASE("ISF playback bounds ordered controls and navigates direct folders", "[isf]") {
    IsfTree tree;
    PlaybackSettings settings{1, false, true, false, 1};
    IsfPlayback playback({".", "a.fs"}, settings, 7);
    for (int n = 0; n < 64; ++n)
        REQUIRE(playback.route(cmd::IsfParam{"level", {int32_t(n)}}) == IsfControlRoute::Queued);
    REQUIRE(playback.route(cmd::IsfEvent{"reset"}) == IsfControlRoute::Full);
    REQUIRE(std::get<int32_t>(std::get<cmd::IsfParam>(playback.controls().back()).values[0]) == 63);
    playback.setCatalog(catalog(tree));
    auto first = playback.takeRequest();
    REQUIRE(playback.commit(first->generation, first->id));
    REQUIRE(playback.controls().empty());
    playback.presented(2);
    REQUIRE_FALSE(playback.takeRequest());
    playback.navigate(-1, false);
    auto previous = playback.takeRequest();
    REQUIRE(previous->id == "b.fs");
    REQUIRE(playback.commit(previous->generation, previous->id));
    settings.locked = false;
    playback.setSettings(settings);
    playback.presented(2);
    auto automatic = playback.takeRequest();
    REQUIRE(automatic->id == "a.fs");
    REQUIRE(playback.commit(automatic->generation, automatic->id));
    playback.navigate(1, true);
    REQUIRE(playback.takeRequest()->id == "b.fs");
    REQUIRE(playback.selectPlaylist("folder"));
    auto folder = playback.takeRequest();
    REQUIRE(folder->id == "folder/c.fs");
    playback.failed(folder->generation, "bad c");
    REQUIRE_FALSE(playback.takeRequest()); // nested d is not an alternative
    REQUIRE_FALSE(playback.pending());
}

TEST_CASE("ISF queued and immediate controls share ordered atomic validation", "[isf]") {
    IsfInput level{"level", IsfInputKind::Float};
    level.defaultValue = .2;
    IsfInput reset{"reset", IsfInputKind::Event};
    IsfParameters immediate({level, reset}), queued({level, reset});
    immediate.initializeDefaults({});
    queued.initializeDefaults({});
    IsfPlayback playback({".", "a.fs"}, PlaybackSettings{}, 1);
    const std::vector<IsfControlCommand> commands = {
        cmd::IsfParam{"level", {.4f}}, cmd::IsfEvent{"reset"},
        cmd::IsfParam{"level", {.8f, .9f}}, cmd::IsfParam{"level", {.7f}}
    };
    for (const auto& command : commands)
        REQUIRE(playback.route(command) == IsfControlRoute::Queued);
    const bool expectedAccepted[] = {true, true, false, true};
    const double expectedLevel[] = {double(.4f), double(.4f), double(.4f), double(.7f)};
    for (size_t n = 0; n < commands.size(); ++n) {
        std::string immediateError, queuedError;
        REQUIRE(immediate.apply(commands[n], immediateError) == expectedAccepted[n]);
        REQUIRE(queued.apply(playback.controls()[n], queuedError) == expectedAccepted[n]);
        REQUIRE(immediate.values() == queued.values());
        REQUIRE(immediate.events() == queued.events());
        REQUIRE(queuedError == immediateError);
        REQUIRE(std::get<double>(queued.values().at("level")) == expectedLevel[n]);
        REQUIRE(queued.events().count("reset") == (n == 0 ? 0 : 1));
    }
}

TEST_CASE("ISF previous skips broken shaders backwards through wraparound", "[isf]") {
    IsfTree tree;
    for (auto id : {"a.fs", "b.fs", "c.fs", "d.fs"}) tree.file(id, "/*{}*/");
    IsfPlayback playback({".", "c.fs"}, PlaybackSettings{}, 1);
    playback.setCatalog(std::make_shared<IsfCatalog>(IsfCatalog::scan(tree.root, {})));
    auto first = playback.takeRequest();
    REQUIRE(first);
    REQUIRE(playback.commit(first->generation, first->id));
    playback.navigate(-1, false);
    for (auto expected : {"b.fs", "a.fs", "d.fs", "c.fs"}) {
        auto request = playback.takeRequest();
        REQUIRE(request);
        REQUIRE(request->id == expected);
        playback.failed(request->generation, "broken");
    }
    REQUIRE_FALSE(playback.takeRequest());
    REQUIRE_FALSE(playback.pending());
}
