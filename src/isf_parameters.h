#pragma once
#include "command.h"
#include <map>
#include <set>
namespace sideshow {
using IsfControlCommand = std::variant<cmd::IsfParam, cmd::IsfEvent>;
class IsfParameters {
public:
 explicit IsfParameters(std::vector<IsfInput> inputs):inputs_(std::move(inputs)){}
 void initializeDefaults(const std::map<std::string,IsfValue>&);
 bool apply(const IsfControlCommand&,std::string& error);
 bool set(const cmd::IsfParam&,std::string& error);
 bool trigger(std::string_view,std::string& error);
 const std::map<std::string,IsfValue>& values() const{return values_;}
 const std::set<std::string>& events() const{return events_;}
 void presented(){events_.clear();}
private:
 std::vector<IsfInput> inputs_;std::map<std::string,IsfValue> values_;std::set<std::string> events_;
};
}
