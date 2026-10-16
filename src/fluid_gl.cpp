#include "fluid_gl.h"
#include "fluid_shaders.h"
#include <stdexcept>
#include <cmath>
namespace sideshow::fluid_gl {
namespace {
#ifdef SIDESHOW_GLES
const char* header="#version 300 es\nprecision highp float;\nprecision highp sampler2D;\n";
#else
const char* header="#version 330 core\n";
#endif
GLuint compile(GLenum type,const char* body,const std::string& defines) {
    GLuint shader=glCreateShader(type);
    const char* sources[]={header,defines.c_str(),body};
    glShaderSource(shader,3,sources,nullptr);glCompileShader(shader);
    GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok) {char log[4096]={};glGetShaderInfoLog(shader,sizeof(log),nullptr,log);glDeleteShader(shader);throw std::runtime_error(std::string("Fluid shader: ")+log);}
    return shader;
}
}
Program::Program(const char* vertex,const char* fragment,const std::string& defines) {
    GLuint v=0,f=0;
    try {
        v=compile(GL_VERTEX_SHADER,vertex,defines);f=compile(GL_FRAGMENT_SHADER,fragment,defines);
        id_=glCreateProgram();glAttachShader(id_,v);glAttachShader(id_,f);glLinkProgram(id_);
        GLint ok=0;glGetProgramiv(id_,GL_LINK_STATUS,&ok);
        if(!ok){char log[4096]={};glGetProgramInfoLog(id_,sizeof(log),nullptr,log);throw std::runtime_error(std::string("Fluid link: ")+log);}
    } catch(...) {if(v)glDeleteShader(v);if(f)glDeleteShader(f);if(id_)glDeleteProgram(id_);throw;}
    glDeleteShader(v);glDeleteShader(f);
}
Program::~Program(){if(id_)glDeleteProgram(id_);}
void Program::texture(const char* n,GLuint t,int unit) const {glActiveTexture(GL_TEXTURE0+unit);glBindSampler(unit,0);glBindTexture(GL_TEXTURE_2D,t);glUniform1i(location(n),unit);}
GLenum externalFormat(GLenum f){return f==GL_R16F?GL_RED:f==GL_RG16F?GL_RG:GL_RGBA;}
Target::Target(int w,int h,GLenum internal,GLenum format,bool linear):width(w),height(h) {
    GLint limit=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&limit);
    if(w<=0||h<=0||w>limit||h>limit)throw std::runtime_error("Fluid target dimensions unsupported");
    GLint oldTex=0,oldFbo=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldTex);glGetIntegerv(GL_FRAMEBUFFER_BINDING,&oldFbo);
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,linear?GL_LINEAR:GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,linear?GL_LINEAR:GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,internal,w,h,0,format,GL_HALF_FLOAT,nullptr);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    bool ok=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    if(ok) {GLfloat zero[4]={};glClearBufferfv(GL_COLOR,0,zero);}
    glBindFramebuffer(GL_FRAMEBUFFER,oldFbo);glBindTexture(GL_TEXTURE_2D,oldTex);
    if(!ok || glGetError()!=GL_NO_ERROR){glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);throw std::runtime_error("Fluid float framebuffer unsupported or allocation failed");}
}
Target::~Target(){if(fbo)glDeleteFramebuffers(1,&fbo);if(texture)glDeleteTextures(1,&texture);}
Target::Target(Target&& o) noexcept {std::swap(texture,o.texture);std::swap(fbo,o.fbo);std::swap(width,o.width);std::swap(height,o.height);}
Target& Target::operator=(Target&& o) noexcept {std::swap(texture,o.texture);std::swap(fbo,o.fbo);std::swap(width,o.width);std::swap(height,o.height);return *this;}
VertexArray::VertexArray(){glGenVertexArrays(1,&id_);}
VertexArray::~VertexArray(){if(id_)glDeleteVertexArrays(1,&id_);}
void resetState(){glActiveTexture(GL_TEXTURE0);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_CULL_FACE);glDisable(GL_RASTERIZER_DISCARD);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glBlendEquation(GL_FUNC_ADD);}

namespace {
constexpr std::array<GLenum,7> trackedEnables={GL_BLEND,GL_DEPTH_TEST,GL_STENCIL_TEST,GL_SCISSOR_TEST,GL_CULL_FACE,GL_RASTERIZER_DISCARD,GL_DITHER};
}
StateGuard::StateGuard() {
    glGetIntegerv(GL_ACTIVE_TEXTURE,&active_);
    glGetIntegerv(GL_CURRENT_PROGRAM,&program_);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao_);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&drawFbo_);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&readFbo_);
    glGetIntegerv(GL_VIEWPORT,viewport_.data());
    glGetBooleanv(GL_COLOR_WRITEMASK,colorMask_.data());
    for(size_t i=0;i<trackedEnables.size();++i)enabled_[i]=glIsEnabled(trackedEnables[i]);
    glGetIntegerv(GL_BLEND_SRC_RGB,&srcRgb_);glGetIntegerv(GL_BLEND_DST_RGB,&dstRgb_);
    glGetIntegerv(GL_BLEND_SRC_ALPHA,&srcAlpha_);glGetIntegerv(GL_BLEND_DST_ALPHA,&dstAlpha_);
    glGetIntegerv(GL_BLEND_EQUATION_RGB,&equationRgb_);glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&equationAlpha_);
    for(size_t i=0;i<textures_.size();++i) {
        glActiveTexture(GL_TEXTURE0+static_cast<GLenum>(i));
        glGetIntegerv(GL_TEXTURE_BINDING_2D,&textures_[i]);
        glGetIntegerv(GL_SAMPLER_BINDING,&samplers_[i]);
    }
    glActiveTexture(active_);
}
StateGuard::~StateGuard() {
    for(size_t i=0;i<textures_.size();++i) {
        glActiveTexture(GL_TEXTURE0+static_cast<GLenum>(i));
        glBindTexture(GL_TEXTURE_2D,textures_[i]);glBindSampler(static_cast<GLuint>(i),samplers_[i]);
    }
    glActiveTexture(active_);glUseProgram(program_);glBindVertexArray(vao_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,drawFbo_);glBindFramebuffer(GL_READ_FRAMEBUFFER,readFbo_);
    glViewport(viewport_[0],viewport_[1],viewport_[2],viewport_[3]);
    glColorMask(colorMask_[0],colorMask_[1],colorMask_[2],colorMask_[3]);
    for(size_t i=0;i<trackedEnables.size();++i) {
        if(enabled_[i])glEnable(trackedEnables[i]);else glDisable(trackedEnables[i]);
    }
    glBlendFuncSeparate(srcRgb_,dstRgb_,srcAlpha_,dstAlpha_);
    glBlendEquationSeparate(equationRgb_,equationAlpha_);
}

Capabilities probeCapabilities() {
    StateGuard restore;
    resetState();
    Capabilities c;
    // Float attachment completeness is a runtime property, even with advertised extensions.
    auto supported=[](GLenum f){try{Target t(2,2,f,externalFormat(f),false);return true;}catch(const std::exception&){while(glGetError()!=GL_NO_ERROR){}return false;}};
    if(!supported(GL_RGBA16F))throw std::runtime_error("RGBA16F render targets unavailable");
    if(!supported(GL_R16F))c.scalar=GL_RGBA16F;
    if(!supported(GL_RG16F))c.vector=GL_RGBA16F;
    // Probe interpolation by sampling halfway between a black and white float texel.
    {
        Target source(2,1,GL_RGBA16F,GL_RGBA,true),dest(1,1,GL_RGBA16F,GL_RGBA,false);
        float ramp[]={0,0,0,1,1,1,1,1};glBindTexture(GL_TEXTURE_2D,source.texture);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,2,1,GL_RGBA,GL_FLOAT,ramp);
        Program p(fluid_shader::vertex,fluid_shader::copy);VertexArray vao;vao.bind();p.use();
        p.texture("uTexture",source.texture,0);glBindFramebuffer(GL_FRAMEBUFFER,dest.fbo);glViewport(0,0,1,1);glDrawArrays(GL_TRIANGLES,0,3);
        float pixel[4]={};glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,pixel);
        c.linear=glGetError()==GL_NO_ERROR && std::abs(pixel[0]-.5f)<.05f;
    }
    return c;
}
}
