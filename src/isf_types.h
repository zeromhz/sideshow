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
// renderScale: optional "SIDESHOW_RENDER_SCALE" header key (0.25-1); the shader renders at that
// fraction of the capped ISF size, which the presenter stretches to the screen.
struct IsfDocument {std::string id,fragment,vertex;std::vector<IsfInput> inputs;std::vector<IsfPass> passes;std::vector<IsfImport> imports;double renderScale=1;};
}
