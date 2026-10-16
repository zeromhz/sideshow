#include <catch2/catch_test_macros.hpp>
#include "isf_catalog.h"
#include "isf_files.h"
#include "support/isf_tree.h"
using namespace sideshow;
TEST_CASE("ISF exact IDs and direct folders exclude hidden and linked files","[isf]") {
 IsfTree t;t.file("a/shape.fs","a");t.file("b/shape.fs","b");t.file("a/sub/other.FS","c");t.file("a/shape.vs","v");t.file(".hidden.fs","h");t.file(".hidden/a.fs","h");t.symlink("linked",t.root/"b");
 auto c=IsfCatalog::scan(t.root,[]{return false;});CHECK(c.entries().size()==3);
 CHECK(c.inFolder("a")==std::vector<std::string>{"a/shape.fs"});CHECK_FALSE(c.find("shape.fs"));CHECK_FALSE(c.find("A/shape.fs"));
 IsfFiles files(t.root);CHECK(files.source("a/shape.fs",{})=="a");CHECK(files.vertexFor("a/shape.fs",{})=="v");CHECK_FALSE(files.vertexFor("b/shape.fs",{}));
 CHECK(IsfCatalog::scan(t.root,[]{return true;}).entries().empty());CHECK_THROWS(files.source("a/shape.fs",[]{return true;}));
}
TEST_CASE("ISF reads imports through no-follow directory descriptors","[isf]") {
 IsfTree t;t.file("a/main.fs","shader");t.file("shared/image.png","image");IsfFiles f(t.root);
 CHECK(f.imported("a/main.fs","../shared/image.png",20,{})==std::vector<uint8_t>{'i','m','a','g','e'});
 for(auto path:{"../../escape.png","/tmp/a.png","https://example.com/a.png","../shared//image.png","../shared/./image.png"})CHECK_THROWS(f.imported("a/main.fs",path,20,{}));
 CHECK_THROWS(f.imported("a/main.fs",std::string("bad\0.png",8),20,{}));
 CHECK_THROWS(f.imported("a/main.fs","../shared/image.png",4,{}));
 auto c=IsfCatalog::scan(t.root,{});REQUIRE(c.find("a/main.fs"));
 std::filesystem::rename(t.root/"a",t.root/"old");t.symlink("a",t.root/"old");CHECK_THROWS(f.source("a/main.fs",{}));
 t.symlink("shared/link.png",t.root/"shared/image.png");CHECK_THROWS(f.imported("old/main.fs","../shared/link.png",20,{}));
}
