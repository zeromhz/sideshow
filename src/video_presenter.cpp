#include "video_presenter.h"
#include <cmath>
#include <stdexcept>
namespace sideshow {
namespace {
const char* vertex=R"(
out vec2 uv;
void main() {
    vec2 pos=vec2(float((gl_VertexID & 1)<<2)-1.0,float((gl_VertexID & 2)<<1)-1.0);
    uv=(pos+1.0)*0.5; gl_Position=vec4(pos,0,1);
})";
const char* fragment=R"(
in vec2 uv;
uniform sampler2D image;
uniform int rotation;
out vec4 color;
void main() {
    vec2 p=vec2(uv.x,1.0-uv.y);
    if(rotation==90) p=vec2(1.0-p.y,p.x);
    else if(rotation==180) p=1.0-p;
    else if(rotation==270) p=vec2(p.y,1.0-p.x);
    color=vec4(texture(image,p).rgb,1.0);
})";
struct UnpackGuard {
    GLint pbo,alignment,row,skipRows,skipPixels;
    UnpackGuard() {
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&pbo);glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH,&row);glGetIntegerv(GL_UNPACK_SKIP_ROWS,&skipRows);glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&skipPixels);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    }
    ~UnpackGuard() {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,row);glPixelStorei(GL_UNPACK_SKIP_ROWS,skipRows);glPixelStorei(GL_UNPACK_SKIP_PIXELS,skipPixels);
    }
};
}
VideoPresenter::VideoPresenter(int w,int h):program_(vertex,fragment),width_(w),height_(h) {
    if(w<=0 || h<=0) throw std::runtime_error("Invalid video drawable size");
}
VideoPresenter::~VideoPresenter() {if(texture_) glDeleteTextures(1,&texture_);}
void VideoPresenter::resize(int w,int h) {if(w>0 && h>0) {width_=w;height_=h;}}
void VideoPresenter::upload(const VideoFrame& frame) {
    GLint limit=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&limit);
    if(frame.width<=0 || frame.height<=0 || frame.width>1920 || frame.height>1920 ||
       frame.width>limit || frame.height>limit || frame.stride<frame.width*4 || frame.stride%4 ||
       size_t(frame.stride)*size_t(frame.height)>frame.rgba.size() ||
       !std::isfinite(frame.pixelAspect) || frame.pixelAspect<=0)
        throw std::runtime_error("Invalid video frame dimensions or stride");
    // Same-sized frames overwrite the existing texture instead of allocating a new one each
    // frame. A failed upload raises a GL error and leaves the old picture untouched.
    if(texture_ && frame.width==textureWidth_ && frame.height==textureHeight_) {
        {
        fluid_gl::StateGuard state;UnpackGuard unpack;
        glActiveTexture(GL_TEXTURE0);glBindSampler(0,0);glBindTexture(GL_TEXTURE_2D,texture_);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,frame.stride/4);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,frame.width,frame.height,GL_RGBA,GL_UNSIGNED_BYTE,frame.rgba.data());
        if(glGetError()!=GL_NO_ERROR) throw std::runtime_error("Video texture upload failed");
        }
        rotation_=frame.rotationDegrees;
        aspect_=double(frame.width)*frame.pixelAspect/frame.height;
        if(rotation_==90 || rotation_==270) aspect_=1.0/aspect_;
        return;
    }
    GLuint candidate=0;
    {
    fluid_gl::StateGuard state;UnpackGuard unpack;
    glActiveTexture(GL_TEXTURE0);glBindSampler(0,0);
    glGenTextures(1,&candidate);glBindTexture(GL_TEXTURE_2D,candidate);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,frame.stride/4);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,frame.width,frame.height,0,GL_RGBA,GL_UNSIGNED_BYTE,frame.rgba.data());
    if(!candidate || glGetError()!=GL_NO_ERROR) {if(candidate) glDeleteTextures(1,&candidate);throw std::runtime_error("Video texture upload failed");}
    }
    if(texture_) glDeleteTextures(1,&texture_);
    texture_=candidate;textureWidth_=frame.width;textureHeight_=frame.height;rotation_=frame.rotationDegrees;
    aspect_=double(frame.width)*frame.pixelAspect/frame.height;
    if(rotation_==90 || rotation_==270) aspect_=1.0/aspect_;
}
void VideoPresenter::render() {
    fluid_gl::resetState();glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,width_,height_);
    const GLfloat black[]={0,0,0,1};glClearBufferfv(GL_COLOR,0,black);
    if(!texture_) return;
    int w=width_,h=height_;
    if(double(w)/h>aspect_) w=std::max(1,int(h*aspect_));else h=std::max(1,int(w/aspect_));
    glViewport((width_-w)/2,(height_-h)/2,w,h);
    program_.use();vao_.bind();program_.texture("image",texture_,0);glUniform1i(program_.location("rotation"),rotation_);
    glDrawArrays(GL_TRIANGLES,0,3);
    if(glGetError()!=GL_NO_ERROR) throw std::runtime_error("Video rendering failed");
}
}
