#include "isf_images.h"
#include "isf_budget.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 4096
#include "stb_image.h"
#include <memory>
#include <stdexcept>
namespace sideshow {
size_t isfImageBytes(const std::vector<uint8_t>& encoded){
 if(encoded.empty()||encoded.size()>32u*1024*1024)throw std::runtime_error("ISF encoded image outside limits");
 int w=0,h=0,channels=0;if(!stbi_info_from_memory(encoded.data(),int(encoded.size()),&w,&h,&channels)||w<=0||h<=0||w>4096||h>4096||uint64_t(w)*h>8847360)throw std::runtime_error("Invalid or oversized ISF PNG/JPEG");
 return isfTextureBytes(w,h,4);
}
IsfImage decodeIsfImage(std::string name,const std::vector<uint8_t>& encoded){
 size_t count=isfImageBytes(encoded);int w=0,h=0,channels=0;
 std::unique_ptr<unsigned char,decltype(&stbi_image_free)> pixels(stbi_load_from_memory(encoded.data(),int(encoded.size()),&w,&h,&channels,4),stbi_image_free);
 if(!pixels)throw std::runtime_error("Cannot decode ISF PNG/JPEG");
 return {std::move(name),w,h,{pixels.get(),pixels.get()+count}};
}
}
