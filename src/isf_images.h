#pragma once
#include "isf_types.h"
namespace sideshow {
struct IsfImage {std::string name;int width=0,height=0;std::vector<uint8_t> rgba;};
struct IsfSource {IsfDocument document;std::vector<IsfImage> images;};
size_t isfImageBytes(const std::vector<uint8_t>& encoded);
IsfImage decodeIsfImage(std::string name,const std::vector<uint8_t>& encoded);
}
