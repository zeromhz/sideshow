#pragma once
#include <filesystem>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>
namespace sideshow {
struct IsfEntry {std::string id,folder;};
class IsfCatalog {
public:
 static IsfCatalog scan(const std::filesystem::path&,const std::function<bool()>&);
 const std::vector<IsfEntry>& entries() const{return entries_;}
 const std::vector<std::string>& warnings() const{return warnings_;}
 std::vector<std::string> inFolder(std::string_view) const;
 std::optional<IsfEntry> find(std::string_view) const;
private:
 std::vector<IsfEntry> entries_;std::vector<std::string> warnings_;
};
}
