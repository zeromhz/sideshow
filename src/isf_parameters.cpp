#include "isf_parameters.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace sideshow {
bool IsfParameters::apply(const IsfControlCommand& command,std::string& error){
 return std::visit([&](const auto& value){
  if constexpr(std::is_same_v<std::decay_t<decltype(value)>,cmd::IsfParam>)return set(value,error);
  else return trigger(value.name,error);
 },command);
}
void IsfParameters::initializeDefaults(const std::map<std::string,IsfValue>& defaults){
 std::map<std::string,IsfValue> next;
 for(const auto& i:inputs_){if(i.kind==IsfInputKind::Event||i.kind==IsfInputKind::Audio||i.kind==IsfInputKind::AudioFFT)continue;
  auto d=defaults.find(i.name);if(!i.defaultValue&&d==defaults.end())throw std::runtime_error("Missing runtime default for "+i.name);next[i.name]=i.defaultValue?*i.defaultValue:d->second;
 }values_=std::move(next);events_.clear();
}
bool IsfParameters::set(const cmd::IsfParam& command,std::string& error){
 auto reject=[&](const char* why){error=why;return false;};
 auto i=std::find_if(inputs_.begin(),inputs_.end(),[&](const auto& v){return v.name==command.name;});
 if(i==inputs_.end())return reject("Unknown ISF parameter");
 size_t count=i->kind==IsfInputKind::Point2D?2:i->kind==IsfInputKind::Color?4:1;
 if(command.values.size()!=count)return reject("Wrong ISF parameter arity");
 std::vector<double> v;for(const auto& n:command.values){double x=std::visit([](auto x){return double(x);},n);if(!std::isfinite(x))return reject("Non-finite ISF parameter");v.push_back(x);}
 if(i->kind==IsfInputKind::Event||i->kind==IsfInputKind::Audio||i->kind==IsfInputKind::AudioFFT)return reject("Input cannot be assigned with isf_param");
 if(i->kind==IsfInputKind::Long||i->kind==IsfInputKind::Bool){auto n=std::get_if<int32_t>(&command.values[0]);if(!n)return reject("Input requires OSC int32");
  if(i->kind==IsfInputKind::Bool){if(*n!=0&&*n!=1)return reject("Boolean requires 0 or 1");values_[i->name]=(*n==1);return true;}
  if(!i->choices.empty()&&std::find(i->choices.begin(),i->choices.end(),*n)==i->choices.end())return reject("Value is not a menu choice");
 }
 auto bounds=[](const IsfValue& value){return std::visit([](const auto& x)->std::vector<double>{using T=std::decay_t<decltype(x)>;if constexpr(std::is_arithmetic_v<T>)return {double(x)};else return {x.begin(),x.end()};},value);};
 if(i->minimum){auto b=bounds(*i->minimum);for(size_t n=0;n<count;++n)v[n]=std::max(v[n],b[n]);}
 if(i->maximum){auto b=bounds(*i->maximum);for(size_t n=0;n<count;++n)v[n]=std::min(v[n],b[n]);}
 IsfValue result;
 switch(i->kind){case IsfInputKind::Long:if(!i->choices.empty()&&std::find(i->choices.begin(),i->choices.end(),int32_t(v[0]))==i->choices.end())return reject("Bounded value is not a menu choice");result=int32_t(v[0]);break;
 case IsfInputKind::Float:result=v[0];break;
 case IsfInputKind::Point2D:result=std::array<double,2>{v[0],v[1]};break;
 case IsfInputKind::Color:result=std::array<double,4>{v[0],v[1],v[2],v[3]};break;
 default:return reject("Unsupported ISF assignment");}
 values_[i->name]=result;error.clear();return true;
}
bool IsfParameters::trigger(std::string_view name,std::string& error){for(const auto& i:inputs_)if(i.name==name&&i.kind==IsfInputKind::Event){events_.insert(i.name);error.clear();return true;}error="Unknown ISF event";return false;}
}
