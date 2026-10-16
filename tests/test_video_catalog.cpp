#include <catch2/catch_test_macros.hpp>
#include "video_catalog.h"
#include "video_types.h"
#include "support/video_tree.h"
using namespace sideshow;
TEST_CASE("Video IDs preserve nested duplicate names and direct folder collections") {
    VideoTree tree;tree.file("intro.mp4");tree.file("a/intro.mp4");tree.file("a/b/deep.MOV");tree.file("a/z.webm");
    auto catalog=VideoCatalog::scan(tree.root,[]{return false;});
    CHECK(catalog.inFolder(".")==std::vector<std::string>{"intro.mp4"});
    CHECK(catalog.inFolder("a")==std::vector<std::string>{"a/intro.mp4","a/z.webm"});
    REQUIRE(catalog.find("a/b/deep.MOV"));
    CHECK_FALSE(catalog.find("deep.MOV"));CHECK_FALSE(catalog.find("a/b/deep.mov"));
    CHECK_FALSE(catalog.resolveForOpen("../outside.mp4"));
    CHECK_FALSE(catalog.resolveForOpen("/etc/passwd"));
}
TEST_CASE("Video library skips hidden symlink and nonvideo entries and rechecks replacements") {
    VideoTree tree;tree.file("visible.mp4");tree.file(".hidden.mp4");tree.file(".hidden/a.mp4");tree.file("text.txt");
    tree.symlink("link.mp4",tree.root/"visible.mp4");tree.symlink("cycle",tree.root);
    auto catalog=VideoCatalog::scan(tree.root,[]{return false;});
    REQUIRE(catalog.clips().size()==1);
    std::filesystem::remove(tree.root/"visible.mp4");
    CHECK_FALSE(catalog.resolveForOpen("visible.mp4"));
    tree.symlink("visible.mp4",tree.root/".hidden.mp4");
    CHECK_FALSE(catalog.resolveForOpen("visible.mp4"));
    CHECK(VideoCatalog::scan(tree.root/"cycle",[]{return false;}).clips().empty());
}
TEST_CASE("Video scan cancellation and inaccessible paths retain a safe catalog") {
    VideoTree tree;tree.file("a/clip.mp4");tree.file("b/clip.mp4");
    auto cancelled=VideoCatalog::scan(tree.root,[]{return true;});
    CHECK(cancelled.clips().empty());CHECK_FALSE(cancelled.warnings().empty());
    auto missing=VideoCatalog::scan(tree.root/"missing",[]{return false;});
    CHECK(missing.clips().empty());CHECK_FALSE(missing.warnings().empty());
    std::filesystem::permissions(tree.root/"a",std::filesystem::perms::none);
    auto catalog=VideoCatalog::scan(tree.root,[]{return false;});
    std::filesystem::permissions(tree.root/"a",std::filesystem::perms::owner_all);
    CHECK(catalog.find("b/clip.mp4"));
    if(catalog.find("a/clip.mp4")) WARN("Permission denial unavailable for this test identity");
    else CHECK_FALSE(catalog.warnings().empty());
}
TEST_CASE("Video IDs reject traversal and protocol syntax") {
    for(auto id:{"", ".", "..", "a/../b.mp4", "a//b.mp4", "/b.mp4", "file:b.mp4", "http://x/b.mp4", "a\\b.mp4", "a/./b.mp4"}) CHECK_FALSE(validVideoId(id));
    CHECK_FALSE(validVideoId(std::string("a\0b.mp4",7)));
    CHECK(validVideoId("A folder/My clip.MP4"));
}
