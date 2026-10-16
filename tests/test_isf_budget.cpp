#include <catch2/catch_test_macros.hpp>
#include "isf_budget.h"
using namespace sideshow;
TEST_CASE("ISF reservations enforce both owner and combined ceilings","[isf]") {
 IsfBudget budget;auto a=budget.newOwner(),b=budget.newOwner(),c=budget.newOwner();
 {auto first=budget.reserve(a,128u*1024*1024);CHECK_THROWS(budget.reserve(a,1));auto second=budget.reserve(b,128u*1024*1024);CHECK_THROWS(budget.reserve(c,1));auto moved=std::move(first);CHECK(budget.used()==256u*1024*1024);}
 CHECK(budget.used()==0);CHECK(isfTextureBytes(4096,4096,16)==268435456);CHECK_THROWS(isfTextureBytes(4097,1,4));CHECK_THROWS(isfTextureBytes(1,0,4));CHECK_THROWS(isfTextureBytes(4096,4096,SIZE_MAX));
}
TEST_CASE("ISF empty reservations do not create releasable storage","[isf]") {
 IsfBudget b;auto id=b.newOwner();auto a=b.reserve(id,0);{auto empty=b.reserve(id,0);}CHECK(b.used()==0);
}
