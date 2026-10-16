#pragma once
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unistd.h>
struct VideoTree {
    std::filesystem::path root;
    VideoTree() {
        std::string pattern=(std::filesystem::temp_directory_path()/"sideshow-video-XXXXXX").string();
        if(!mkdtemp(pattern.data())) throw std::runtime_error("temporary video directory failed");
        root=pattern;
    }
    ~VideoTree(){std::error_code ec;std::filesystem::remove_all(root,ec);}
    void file(const std::string& name) {
        std::filesystem::create_directories((root/name).parent_path());
        std::ofstream(root/name)<<"fixture";
    }
    void symlink(const std::string& name,const std::filesystem::path& target) {
        std::filesystem::create_symlink(target,root/name);
    }
};
