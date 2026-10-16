#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>
namespace sideshow {
using IsfValue=std::variant<bool,int32_t,double,std::array<double,2>,std::array<double,4>>;
using IsfNumber=std::variant<int32_t,float>;
enum class IsfInputKind {Event,Bool,Long,Float,Point2D,Color,Audio,AudioFFT};
struct IsfInput {
 std::string name; IsfInputKind kind;
 std::optional<IsfValue> defaultValue,minimum,maximum;
 std::vector<int32_t> choices;size_t audioWidth=512;
};
struct IsfPass {std::string target,width,height;bool persistent=false,floating=false;};
struct IsfImport {std::string name,path;};
struct IsfDocument {std::string id,fragment,vertex;std::vector<IsfInput> inputs;std::vector<IsfPass> passes;std::vector<IsfImport> imports;};
}
