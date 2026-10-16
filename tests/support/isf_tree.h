#pragma once
#include "video_tree.h"
struct IsfTree: VideoTree {
 void file(const std::string& name,const std::string& contents) {
  std::filesystem::create_directories((root/name).parent_path());std::ofstream(root/name,std::ios::binary)<<contents;
 }
};
