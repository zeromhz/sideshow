#pragma once
#include "isf_types.h"
#include <string_view>
namespace sideshow {
IsfDocument parseIsfDocument(std::string id,std::string fragment,std::string vertex);
bool validIsfId(std::string_view id);
std::string resolveIsfImport(std::string_view shaderId,std::string_view relative);
}
