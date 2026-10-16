#include <catch2/catch_test_macros.hpp>
#include "isf_images.h"
#include <fstream>
using namespace sideshow;
static std::vector<uint8_t> bytes(const char* name){std::ifstream in(std::string(ISF_FIXTURE_DIR)+"/images/"+name,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
TEST_CASE("ISF decodes bounded PNG and JPEG memory snapshots","[isf]") {
 auto png=bytes("pixel.png");auto image=decodeIsfImage("x",png);CHECK(image.width==1);CHECK(image.height==1);CHECK(image.rgba==std::vector<uint8_t>{255,0,0,128});
 auto jpg=decodeIsfImage("x",bytes("pixel.jpg"));CHECK(jpg.width==1);CHECK(jpg.rgba.size()==4);CHECK(jpg.rgba[3]==255);
 CHECK_THROWS(decodeIsfImage("x",{}));png.resize(12);CHECK_THROWS(decodeIsfImage("x",png));CHECK_THROWS(decodeIsfImage("x",bytes("huge.png")));
}
