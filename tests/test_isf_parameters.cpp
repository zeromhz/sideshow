#include <catch2/catch_test_macros.hpp>
#include "isf_parameters.h"
#include "isf_document.h"
#include "osc_parse.h"
using namespace sideshow;
TEST_CASE("ISF updates are typed atomic and honor declared bounds only","[isf]") {
 auto d=parseIsfDocument("x.fs",R"(/*{"INPUTS":[{"NAME":"speed","TYPE":"float"},{"NAME":"position","TYPE":"point2D","MIN":[0,-1],"MAX":[1,2]},{"NAME":"choice","TYPE":"long","VALUES":[2,4]},{"NAME":"toggle","TYPE":"bool"},{"NAME":"reset","TYPE":"event"}]}*/void main(){})","");
 IsfParameters p(d.inputs);p.initializeDefaults({{"speed",0.0},{"position",std::array<double,2>{0,0}},{"choice",int32_t{2}},{"toggle",false}});std::string error;
 REQUIRE(p.set({"speed",{7.0f}},error));CHECK(std::get<double>(p.values().at("speed"))==7);
 CHECK_FALSE(p.set({"speed",{1.0f,2.0f}},error));CHECK(std::get<double>(p.values().at("speed"))==7);
 REQUIRE(p.set({"position",{2.0f,-2.0f}},error));CHECK(std::get<std::array<double,2>>(p.values().at("position"))==std::array<double,2>{1,-1});
 CHECK_FALSE(p.set({"choice",{int32_t{3}}},error));CHECK_FALSE(p.set({"choice",{4.0f}},error));CHECK(p.set({"choice",{int32_t{4}}},error));
 CHECK_FALSE(p.set({"toggle",{int32_t{2}}},error));CHECK_FALSE(p.set({"toggle",{1.0f}},error));CHECK(p.set({"toggle",{int32_t{1}}},error));
 CHECK_FALSE(p.set({"reset",{int32_t{1}}},error));CHECK_FALSE(p.trigger("speed",error));CHECK(p.trigger("reset",error));CHECK(p.trigger("reset",error));CHECK(p.events().size()==1);p.presented();CHECK(p.events().empty());
}
TEST_CASE("ISF OSC parsing preserves typed multi-value commands","[isf]") {
 auto p=parseOscMessage("p","/sideshow/all/isf_param",{std::string("speed"),0.5f});REQUIRE(p);CHECK(std::get<cmd::IsfParam>(*p).values.size()==1);
 CHECK(parseOscMessage("p","/sideshow/p/isf_event",{std::string("reset")}));
 CHECK_FALSE(parseOscMessage("p","/sideshow/p/isf_param",{std::string("x"),std::string("1")}));
 CHECK_FALSE(parseOscMessage("p","/sideshow/p/isf_param",{std::string("x"),1.0f,2.0f,3.0f}));
 CHECK_FALSE(parseOscMessage("p","/sideshow/p/isf_event",{std::string("reset"),int32_t{1}}));
}
