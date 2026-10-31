#pragma once
#include "gl.h"
#include <string>
#include <array>
#include <utility>
#include <vector>
#include <cstring>
namespace sideshow::fluid_gl {
// Initialization must not disturb a retained backend, even when construction throws.
class StateGuard {
public:
    StateGuard();
    ~StateGuard();
    StateGuard(const StateGuard&)=delete;
    StateGuard& operator=(const StateGuard&)=delete;
private:
    GLint active_=0,program_=0,vao_=0,drawFbo_=0,readFbo_=0;
    std::array<GLint,4> textures_{},samplers_{},viewport_{};
    std::array<GLboolean,4> colorMask_{};
    std::array<GLboolean,7> enabled_{};
    GLint srcRgb_=0,dstRgb_=0,srcAlpha_=0,dstAlpha_=0,equationRgb_=0,equationAlpha_=0;
};
class Program {
public:
    Program(const char* vertex,const char* fragment,const std::string& defines="");
    ~Program();
    Program(const Program&)=delete;Program& operator=(const Program&)=delete;
    void use() const {glUseProgram(id_);}
    // Locations are cached per name (callers pass string literals, so the pointer check
    // usually hits); looking them up by name on every set cost ~150 calls per Fluid frame.
    GLint location(const char* n) const {
        for(const auto& e:locations_) if(e.first==n || std::strcmp(e.first,n)==0) return e.second;
        GLint l=glGetUniformLocation(id_,n);locations_.emplace_back(n,l);return l;
    }
    void one(const char* n,float v) const {glUniform1f(location(n),v);}
    void two(const char* n,float a,float b) const {glUniform2f(location(n),a,b);}
    void three(const char* n,float a,float b,float c) const {glUniform3f(location(n),a,b,c);}
    void texture(const char* n,GLuint texture,int unit) const;
private: GLuint id_=0;mutable std::vector<std::pair<const char*,GLint>> locations_;
};
class Target {
public:
    Target(int width,int height,GLenum internalFormat,GLenum format,bool linear);
    ~Target();
    Target(const Target&)=delete;Target& operator=(const Target&)=delete;
    Target(Target&& other) noexcept;
    Target& operator=(Target&& other) noexcept;
    GLuint texture=0,fbo=0;int width=0,height=0;
};
class VertexArray {
public:
    VertexArray();~VertexArray();
    VertexArray(const VertexArray&)=delete;VertexArray& operator=(const VertexArray&)=delete;
    void bind() const {glBindVertexArray(id_);}
private: GLuint id_=0;
};
struct Capabilities { GLenum scalar=GL_R16F, vector=GL_RG16F; bool linear=false; };
Capabilities probeCapabilities();
GLenum externalFormat(GLenum internalFormat);
void resetState();
}
