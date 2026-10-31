#include "isf_document.h"
#include <nlohmann_json/json.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <set>
#include <stdexcept>
namespace sideshow {
namespace {
using Json=nlohmann::json;
[[noreturn]] void bad(const std::string& why){throw std::runtime_error("ISF metadata: "+why);}
bool letter(char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z');}
bool digit(char c){return c>='0'&&c<='9';}
void name(const std::string& s,std::set<std::string>& names){
 static const std::set<std::string> reserved={"TIME","TIMEDELTA","DATE","FRAMEINDEX","RENDERSIZE","PASSINDEX","inputImage","isf_FragNormCoord","vv_FragNormCoord","main","float","int","bool","vec2","vec3","vec4","sampler2D","uniform","varying","attribute","void","const","return","if","else","for","while"};
 if(s.empty()||!letter(s[0])||!std::all_of(s.begin(),s.end(),[](char c){return letter(c)||digit(c)||c=='_';})||s.find("__")!=s.npos||s.rfind("gl_",0)==0||s.rfind("isf_",0)==0||s.rfind("vv_",0)==0||reserved.count(s)||!names.insert(s).second)bad("invalid, reserved or duplicate name: "+s);
}
double number(const Json& j){if(!j.is_number())bad("expected numeric value");double d=j.get<double>();if(!std::isfinite(d)||std::abs(d)>std::numeric_limits<float>::max())bad("non-finite numeric value");return d;}
int32_t integer(const Json& j){if(!j.is_number_integer())bad("expected integer");double d=number(j);if(d<INT32_MIN||d>INT32_MAX)bad("integer outside int32");return int32_t(d);}
IsfValue value(const Json& j,IsfInputKind kind){
 switch(kind){
 case IsfInputKind::Bool:case IsfInputKind::Event:if(!j.is_boolean())bad("expected boolean");return j.get<bool>();
 case IsfInputKind::Long:return integer(j);
 case IsfInputKind::Float:return number(j);
 case IsfInputKind::Point2D: {if(!j.is_array()||j.size()!=2)bad("expected two coordinates");return std::array<double,2>{number(j[0]),number(j[1])};}
 case IsfInputKind::Color:{if(!j.is_array()||j.size()!=4)bad("expected RGBA");return std::array<double,4>{number(j[0]),number(j[1]),number(j[2]),number(j[3])};}
 default:bad("unexpected audio value");
 }
}
std::vector<double> components(const IsfValue& v){return std::visit([](const auto& x)->std::vector<double>{using T=std::decay_t<decltype(x)>;if constexpr(std::is_arithmetic_v<T>)return {double(x)};else return {x.begin(),x.end()};},v);}
std::string dimension(const Json& obj,const char* key){if(!obj.count(key))return {};if(obj[key].is_string())return obj[key].get<std::string>();double n=number(obj[key]);if(n<=0||n>4096)bad("invalid target dimension");return obj[key].dump();}
}
bool validIsfId(std::string_view id){
 if(id.empty()||id.find_first_of(":\\")!=id.npos||id.find('\0')!=id.npos)return false;
 while(true){auto slash=id.find('/');auto part=id.substr(0,slash);if(part.empty()||part.front()=='.')return false;if(slash==id.npos)return true;id.remove_prefix(slash+1);}
}
std::string resolveIsfImport(std::string_view id,std::string_view relative){
 if(!validIsfId(id)||relative.empty()||relative.front()=='/'||relative.find_first_of(":\\")!=relative.npos||relative.find('\0')!=relative.npos)bad("unsafe import path");
 auto base=std::filesystem::path(id).parent_path();
 while(true){auto slash=relative.find('/');auto part=relative.substr(0,slash);if(part.empty()||part=="."||(part.front()=='.'&&part!=".."))bad("unsafe import component");
  if(part==".."){if(base.empty())bad("import escapes root");base=base.parent_path();}else base/=std::string(part);
  if(slash==relative.npos)break;relative.remove_prefix(slash+1);
 }
 auto result=base.generic_string();if(!validIsfId(result))bad("unsafe import");return result;
}
IsfDocument parseIsfDocument(std::string id,std::string fragment,std::string vertex){
 if(!validIsfId(id)||fragment.size()>1048576||vertex.size()>1048576)bad("invalid ID or oversized source");
 auto start=fragment.find("/*"),end=fragment.find("*/",start==fragment.npos?0:start+2);
 if(start==fragment.npos||end==fragment.npos)bad("missing JSON header");
 try{
 auto j=Json::parse(fragment.substr(start+2,end-start-2));if(!j.is_object())bad("header must be object");
 if(j.count("ISFVSN")){if(!j["ISFVSN"].is_string())bad("invalid version");auto v=j["ISFVSN"].get<std::string>();if(v!="1"&&v!="1.0"&&v!="2"&&v!="2.0")bad("unsupported ISF version");}
 IsfDocument doc;doc.id=std::move(id);doc.fragment=std::move(fragment);doc.vertex=std::move(vertex);std::set<std::string> names;
 if(j.count("INPUTS")){
  auto& inputs=j["INPUTS"];if(!inputs.is_array()||inputs.size()>64)bad("INPUTS must have at most 64 entries");
  for(const auto& obj:inputs){if(!obj.is_object())bad("input must be object");IsfInput in;in.name=obj.at("NAME").get<std::string>();name(in.name,names);auto type=obj.at("TYPE").get<std::string>();
   if(type=="event")in.kind=IsfInputKind::Event;else if(type=="bool")in.kind=IsfInputKind::Bool;else if(type=="long")in.kind=IsfInputKind::Long;else if(type=="float")in.kind=IsfInputKind::Float;else if(type=="point2D")in.kind=IsfInputKind::Point2D;else if(type=="color")in.kind=IsfInputKind::Color;else if(type=="audio")in.kind=IsfInputKind::Audio;else if(type=="audioFFT")in.kind=IsfInputKind::AudioFFT;else bad("unsupported input type: "+type);
   if(in.kind==IsfInputKind::Audio||in.kind==IsfInputKind::AudioFFT){if(obj.count("MAX")){auto n=integer(obj["MAX"]);if(n<=0||n>4096)bad("audio MAX outside 1..4096");in.audioWidth=size_t(n);}}
   else {
    if(obj.count("DEFAULT"))in.defaultValue=value(obj["DEFAULT"],in.kind);
    if(obj.count("MIN"))in.minimum=value(obj["MIN"],in.kind);
    if(obj.count("MAX"))in.maximum=value(obj["MAX"],in.kind);
    if(in.minimum&&in.maximum){auto a=components(*in.minimum),b=components(*in.maximum);for(size_t i=0;i<a.size();++i)if(a[i]>b[i])bad("inverted bounds");}
    if(in.defaultValue){auto d=components(*in.defaultValue);for(auto bound:{in.minimum,in.maximum}){if(!bound)continue;auto b=components(*bound);for(size_t i=0;i<d.size();++i)if((bound==in.minimum&&d[i]<b[i])||(bound==in.maximum&&d[i]>b[i]))bad("default outside bounds");}}
   }
   if(obj.count("VALUES")){if(in.kind!=IsfInputKind::Long||!obj["VALUES"].is_array()||obj["VALUES"].empty())bad("invalid menu VALUES");for(const auto& v:obj["VALUES"])in.choices.push_back(integer(v));if(in.defaultValue&&std::find(in.choices.begin(),in.choices.end(),std::get<int32_t>(*in.defaultValue))==in.choices.end())bad("default absent from menu");}
   if(obj.count("LABELS")){if(!obj["LABELS"].is_array()||obj["LABELS"].size()!=in.choices.size())bad("menu LABELS mismatch");for(const auto& l:obj["LABELS"])if(!l.is_string())bad("menu label must be string");}
   doc.inputs.push_back(std::move(in));
  }
 }
 if(j.count("SIDESHOW_RENDER_SCALE")){double scale=number(j["SIDESHOW_RENDER_SCALE"]);if(scale<0.25||scale>1)bad("SIDESHOW_RENDER_SCALE outside 0.25..1");doc.renderScale=scale;}
 if(j.count("PASSES")){if(!j["PASSES"].is_array()||j["PASSES"].size()>8)bad("PASSES must have at most 8 entries");for(const auto& obj:j["PASSES"]){if(!obj.is_object())bad("pass must be object");IsfPass p;if(obj.count("TARGET")){p.target=obj["TARGET"].get<std::string>();name(p.target,names);}p.width=dimension(obj,"WIDTH");p.height=dimension(obj,"HEIGHT");for(auto key:{"PERSISTENT","FLOAT"})if(obj.count(key)&&!obj[key].is_boolean())bad("pass flags must be boolean");p.persistent=obj.value("PERSISTENT",false);p.floating=obj.value("FLOAT",false);if(p.persistent&&p.target.empty())bad("persistent pass needs TARGET");doc.passes.push_back(std::move(p));}}
 if(j.count("IMPORTED")){auto& imports=j["IMPORTED"];if(!imports.is_object()||imports.size()>16)bad("IMPORTED must have at most 16 named entries");for(auto it=imports.begin();it!=imports.end();++it){name(it.key(),names);auto path=it.value().at("PATH").get<std::string>();resolveIsfImport(doc.id,path);doc.imports.push_back({it.key(),path});}}
 return doc;
 }catch(const Json::exception& e){bad(e.what());}
}
}
