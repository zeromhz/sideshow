#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>
namespace sideshow {
class IsfFiles {
public:
 explicit IsfFiles(std::filesystem::path root);
 std::string source(std::string_view,const std::function<bool()>&) const;
 std::optional<std::string> vertexFor(std::string_view,const std::function<bool()>&) const;
 std::vector<uint8_t> imported(std::string_view,std::string_view,size_t,const std::function<bool()>&) const;
private:
 struct Root;std::shared_ptr<Root> root_;
 std::optional<std::vector<uint8_t>> read(std::string_view,size_t,const std::function<bool()>&,bool optional=false) const;
};
}
