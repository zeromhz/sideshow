#include <catch2/catch_test_macros.hpp>
#include "isf_document.h"
using namespace sideshow;
static IsfDocument parse(const std::string& json) {return parseIsfDocument("a.fs","/*"+json+"*/void main(){}","");}
TEST_CASE("ISF preserves declared metadata without synthesized bounds","[isf]") {
 auto d=parse(R"({"ISFVSN":"2.0","INPUTS":[{"NAME":"speed","TYPE":"float"},{"NAME":"fft","TYPE":"audioFFT","MAX":4096}]})");
 REQUIRE(d.inputs.size()==2);CHECK_FALSE(d.inputs[0].minimum);CHECK_FALSE(d.inputs[0].defaultValue);CHECK(d.inputs[1].audioWidth==4096);
 CHECK(parse("{}").inputs.empty());
}
TEST_CASE("ISF rejects malformed or unsupported metadata before runtime use","[isf]") {
 for(auto json:{R"({"ISFVSN":"3.0"})",R"({"INPUTS":{}})",R"({"INPUTS":[{"NAME":"TIME","TYPE":"float"}]})",
 R"({"INPUTS":[{"NAME":"gl_bad","TYPE":"float"}]})",R"({"INPUTS":[{"NAME":"a-b","TYPE":"float"}]})",
 R"({"INPUTS":[{"NAME":"x","TYPE":"image"}]})",R"({"INPUTS":[{"NAME":"x","TYPE":"float","MIN":2,"MAX":1}]})",
 R"({"INPUTS":[{"NAME":"x","TYPE":"long","VALUES":[2147483648]}]})",
 R"({"INPUTS":[{"NAME":"x","TYPE":"long","VALUES":[1],"LABELS":[]}]})",
 R"({"INPUTS":[{"NAME":"x","TYPE":"color","DEFAULT":[1,0]}]})",
 R"({"INPUTS":[{"NAME":"x","TYPE":"audio","MAX":4097}]})",
 R"({"INPUTS":[{"NAME":"x","TYPE":"bool","DEFAULT":2}]})",
 R"({"INPUTS":[{"NAME":"x","TYPE":"float"},{"NAME":"x","TYPE":"float"}]})",
 R"({"INPUTS":[{"NAME":"x","TYPE":"float"}],"PASSES":[{"TARGET":"x"}]})",
 R"({"PASSES":[{"TARGET":"x"},{"TARGET":"x"}]})",R"({"PASSES":[{"PERSISTENT":"yes"}]})",
 R"({"IMPORTED":{"x":{"PATH":"/tmp/x.png"}}})",R"({"INPUTS":[{"NAME":"x","TYPE":"float","DEFAULT":1e999}]})"}) {
  INFO(json);CHECK_THROWS(parse(json));
 }
 CHECK_THROWS(parseIsfDocument("a.fs",std::string(1048577,'x'),""));
 CHECK_THROWS(parseIsfDocument("a.fs","/*{}*/",std::string(1048577,'x')));
 for(auto section:{"INPUTS","PASSES","IMPORTED"}) {
  std::string json="{\""+std::string(section)+"\":[";
  int count=std::string(section)=="INPUTS"?65:std::string(section)=="PASSES"?9:17;
  for(int i=0;i<count;++i){if(i)json+=',';json+="{\"NAME\":\"x"+std::to_string(i)+"\",\"TYPE\":\"float\",\"PATH\":\"x.png\"}";}
  CHECK_THROWS(parse(json+"]}"));
 }
}
TEST_CASE("ISF render scale is optional and bounded","[isf]") {
 CHECK(parse("{}").renderScale==1);
 CHECK(parse(R"({"SIDESHOW_RENDER_SCALE":0.5})").renderScale==0.5);
 CHECK(parse(R"({"SIDESHOW_RENDER_SCALE":1})").renderScale==1);
 for(auto json:{R"({"SIDESHOW_RENDER_SCALE":0.1})",R"({"SIDESHOW_RENDER_SCALE":1.5})",R"({"SIDESHOW_RENDER_SCALE":"half"})"})
  CHECK_THROWS(parse(json));
}
