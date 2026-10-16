#include "isf_catalog.h"
#include "isf_document.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>
namespace sideshow {
IsfCatalog IsfCatalog::scan(const std::filesystem::path& root,const std::function<bool()>& cancelled){
 IsfCatalog out;
 std::function<void(int,const std::string&)> walk=[&](int fd,const std::string& prefix){
  if(fd<0){out.warnings_.push_back("Cannot scan ISF directory: "+prefix);return;}
  auto raw=fdopendir(fd);if(!raw){close(fd);out.warnings_.push_back("Cannot open ISF directory: "+prefix);return;}
  std::unique_ptr<DIR,decltype(&closedir)> dir(raw,closedir);
  while(!(cancelled&&cancelled())){
   errno=0;auto entry=readdir(dir.get());if(!entry){if(errno)out.warnings_.push_back("ISF directory read failed: "+prefix);break;}
   std::string name(entry->d_name);if(name.empty()||name[0]=='.')continue;
   std::string id=prefix.empty()?name:prefix+"/"+name;if(!validIsfId(id))continue;
   struct stat st{};if(fstatat(fd,name.c_str(),&st,AT_SYMLINK_NOFOLLOW)){out.warnings_.push_back("Cannot inspect ISF entry: "+id);continue;}
   if(S_ISDIR(st.st_mode))walk(openat(fd,name.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC),id);
   else if(S_ISREG(st.st_mode)) {auto ext=std::filesystem::path(name).extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return std::tolower(c);});if(ext==".fs")out.entries_.push_back({id,prefix.empty()?".":prefix});}
  }
 };
 walk(open(root.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC),"");
 if(cancelled&&cancelled()){out.entries_.clear();out.warnings_.push_back("ISF scan cancelled");}
 std::sort(out.entries_.begin(),out.entries_.end(),[](const auto& a,const auto& b){return a.id<b.id;});return out;
}
std::vector<std::string> IsfCatalog::inFolder(std::string_view folder)const{std::vector<std::string> out;for(const auto& e:entries_)if(e.folder==folder)out.push_back(e.id);return out;}
std::optional<IsfEntry> IsfCatalog::find(std::string_view id)const{auto i=std::lower_bound(entries_.begin(),entries_.end(),id,[](const auto& e,auto key){return e.id<key;});if(i!=entries_.end()&&i->id==id)return *i;return std::nullopt;}
}
