#include "isf_files.h"
#include "isf_document.h"
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdexcept>
namespace sideshow {
namespace {
struct Fd {int value;explicit Fd(int v):value(v){}~Fd(){if(value>=0)close(value);}Fd(const Fd&)=delete;Fd& operator=(const Fd&)=delete;};
void checkCancel(const std::function<bool()>& c){if(c&&c())throw std::runtime_error("ISF read cancelled");}
}
struct IsfFiles::Root {int fd;explicit Root(const std::filesystem::path& p):fd(open(p.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)){if(fd<0)throw std::runtime_error("ISF root is inaccessible or unsafe");}~Root(){close(fd);}};
IsfFiles::IsfFiles(std::filesystem::path root):root_(std::make_shared<Root>(root)){}
std::optional<std::vector<uint8_t>> IsfFiles::read(std::string_view id,size_t cap,const std::function<bool()>& cancelled,bool optional) const {
 if(!validIsfId(id))throw std::runtime_error("Invalid ISF path");
 checkCancel(cancelled);Fd directory(dup(root_->fd));
 while(true){auto slash=id.find('/');if(slash==id.npos)break;
  std::string component(id.substr(0,slash));int next=openat(directory.value,component.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
  if(next<0)throw std::runtime_error("Unsafe ISF directory");close(directory.value);directory.value=next;id.remove_prefix(slash+1);checkCancel(cancelled);
 }
 Fd file(openat(directory.value,std::string(id).c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK));
 if(file.value<0){if(optional&&errno==ENOENT)return std::nullopt;throw std::runtime_error("ISF file is inaccessible or unsafe");}
 struct stat st{};if(fstat(file.value,&st)||!S_ISREG(st.st_mode)||st.st_size<0||uint64_t(st.st_size)>cap)throw std::runtime_error("ISF file exceeds size limit or is not regular");
 std::vector<uint8_t> bytes;bytes.reserve(size_t(st.st_size));uint8_t buffer[16384];
 while(true){checkCancel(cancelled);ssize_t n=::read(file.value,buffer,sizeof buffer);if(n<0){if(errno==EINTR)continue;throw std::runtime_error("ISF read failed");}if(!n)break;
  if(size_t(n)>cap-bytes.size())throw std::runtime_error("ISF file exceeds size limit");bytes.insert(bytes.end(),buffer,buffer+n);
 }return bytes;
}
std::string IsfFiles::source(std::string_view id,const std::function<bool()>& c) const {auto b=read(id,1048576,c);return {b->begin(),b->end()};}
std::optional<std::string> IsfFiles::vertexFor(std::string_view id,const std::function<bool()>& c) const {
 if(!validIsfId(id))throw std::runtime_error("Invalid shader ID");auto p=std::filesystem::path(id);p.replace_extension(".vs");auto b=read(p.generic_string(),1048576,c,true);if(!b)return std::nullopt;return std::string(b->begin(),b->end());
}
std::vector<uint8_t> IsfFiles::imported(std::string_view id,std::string_view relative,size_t cap,const std::function<bool()>& c) const {return *read(resolveIsfImport(id,relative),cap,c);}
}
