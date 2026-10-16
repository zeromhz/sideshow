#pragma once
#include "gl.h"
#include <string>
#include <utility>
namespace sideshow::fluid_gl {
class Program {
public:
    Program(const char* vertex,const char* fragment,const std::string& defines="");
    ~Program();
    Program(const Program&)=delete;Program& operator=(const Program&)=delete;
    void use() const {glUseProgram(id_);}
    GLint location(const char* n) const {return glGetUniformLocation(id_,n);}
    void one(const char* n,float v) const {glUniform1f(location(n),v);}
    void two(const char* n,float a,float b) const {glUniform2f(location(n),a,b);}
    void three(const char* n,float a,float b,float c) const {glUniform3f(location(n),a,b,c);}
    void texture(const char* n,GLuint texture,int unit) const;
private: GLuint id_=0;
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
