#pragma once
// Test-build interposition: all wrappers still execute real GL operations.
// Production targets compile fluid_gl.cpp normally, without this header.
#include "gl.h"
#include <array>
#include <set>
namespace fluid_gl_tracking {
inline std::array<std::set<GLuint>,5> live;
inline int failCompileAfter=-1;
inline GLuint createShader(GLenum type) {auto id=::glCreateShader(type);if(id)live[0].insert(id);return id;}
inline void deleteShader(GLuint id) {::glDeleteShader(id);live[0].erase(id);}
inline GLuint createProgram() {auto id=::glCreateProgram();if(id)live[1].insert(id);return id;}
inline void deleteProgram(GLuint id) {::glDeleteProgram(id);live[1].erase(id);}
inline void genTextures(GLsizei n,GLuint* ids) {::glGenTextures(n,ids);for(int i=0;i<n;++i)live[2].insert(ids[i]);}
inline void deleteTextures(GLsizei n,const GLuint* ids) {::glDeleteTextures(n,ids);for(int i=0;i<n;++i)live[2].erase(ids[i]);}
inline void genFramebuffers(GLsizei n,GLuint* ids) {::glGenFramebuffers(n,ids);for(int i=0;i<n;++i)live[3].insert(ids[i]);}
inline void deleteFramebuffers(GLsizei n,const GLuint* ids) {::glDeleteFramebuffers(n,ids);for(int i=0;i<n;++i)live[3].erase(ids[i]);}
inline void genVertexArrays(GLsizei n,GLuint* ids) {::glGenVertexArrays(n,ids);for(int i=0;i<n;++i)live[4].insert(ids[i]);}
inline void deleteVertexArrays(GLsizei n,const GLuint* ids) {::glDeleteVertexArrays(n,ids);for(int i=0;i<n;++i)live[4].erase(ids[i]);}
inline void compileShader(GLuint id) {
    if(failCompileAfter==0) {
        const char* invalid="intentional compile failure";
        ::glShaderSource(id,1,&invalid,nullptr);
        failCompileAfter=-1;
    } else if(failCompileAfter>0) --failCompileAfter;
    ::glCompileShader(id);
}
}
#define glCreateShader fluid_gl_tracking::createShader
#define glDeleteShader fluid_gl_tracking::deleteShader
#define glCreateProgram fluid_gl_tracking::createProgram
#define glDeleteProgram fluid_gl_tracking::deleteProgram
#define glGenTextures fluid_gl_tracking::genTextures
#define glDeleteTextures fluid_gl_tracking::deleteTextures
#define glGenFramebuffers fluid_gl_tracking::genFramebuffers
#define glDeleteFramebuffers fluid_gl_tracking::deleteFramebuffers
#define glGenVertexArrays fluid_gl_tracking::genVertexArrays
#define glDeleteVertexArrays fluid_gl_tracking::deleteVertexArrays
#define glCompileShader fluid_gl_tracking::compileShader
