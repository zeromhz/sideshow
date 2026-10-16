#include <catch2/catch_test_macros.hpp>
#include "isf_loader.h"
#include "support/isf_tree.h"
#include <thread>
using namespace sideshow;
static IsfLoadEvent wait(IsfLoader& loader,uint64_t session){for(int i=0;i<400;++i){if(auto e=loader.poll(session))return *e;std::this_thread::sleep_for(std::chrono::milliseconds(5));}throw std::runtime_error("loader timed out");}
TEST_CASE("ISF loader publishes only current session and generation","[isf]") {
 IsfTree tree;tree.file("a.fs","/*{}*/void main(){}");IsfLoader loader(tree.root);auto session=loader.acquireSession();CHECK(wait(loader,session).kind==IsfLoadKind::Catalog);
 for(int n=1;n<=100;++n)loader.request(session,n,"a.fs");auto e=wait(loader,session);CHECK(e.kind==IsfLoadKind::Source);CHECK(e.generation==100);REQUIRE(e.source);CHECK(e.source->document.id=="a.fs");
 loader.request(session,101,"missing.fs");CHECK(wait(loader,session).kind==IsfLoadKind::Failed);
 loader.cancel(session);loader.request(session,102,"a.fs");CHECK_FALSE(loader.poll(session));
 auto next=loader.acquireSession();CHECK(wait(loader,next).kind==IsfLoadKind::Catalog);CHECK_FALSE(loader.poll(session));
}
