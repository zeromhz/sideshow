#include "video_presenter.h"
#include <array>
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
uniform sampler2D chromaU;
uniform sampler2D chromaV;
uniform int planar;
uniform mat3 yuvToRgb;
uniform vec3 yuvOffset;
uniform int rotation;
out vec4 color;
void main() {
    vec2 p=vec2(uv.x,1.0-uv.y);
    if(rotation==90) p=vec2(1.0-p.y,p.x);
    else if(rotation==180) p=1.0-p;
    else if(rotation==270) p=vec2(p.y,1.0-p.x);
    vec3 rgb;
    if(planar==1) {
        vec3 yuv=vec3(texture(image,p).r,texture(chromaU,p).r,texture(chromaV,p).r);
        rgb=clamp(yuvToRgb*(yuv-yuvOffset),0.0,1.0);
    } else rgb=texture(image,p).rgb;
    color=vec4(rgb,1.0);
})";
// Column-major YUV->RGB matrix and offset for normalized 8-bit samples.
void yuvTransform(VideoMatrix matrix,bool fullRange,GLfloat out[9],GLfloat offset[3]) {
    const double kr=matrix==VideoMatrix::Bt601 ? .299 : matrix==VideoMatrix::Bt2020 ? .2627 : .2126;
    const double kb=matrix==VideoMatrix::Bt601 ? .114 : matrix==VideoMatrix::Bt2020 ? .0593 : .0722;
    const double kg=1-kr-kb;
    const double ys=fullRange ? 1 : 255.0/219, cs=fullRange ? 1 : 255.0/224;
    // R = Y + 2(1-Kr) V;  G = Y - 2Kb(1-Kb)/Kg U - 2Kr(1-Kr)/Kg V;  B = Y + 2(1-Kb) U
    const double m[3][3]={{ys,0,2*(1-kr)*cs},{ys,-2*kb*(1-kb)/kg*cs,-2*kr*(1-kr)/kg*cs},{ys,2*(1-kb)*cs,0}};
    for(int column=0;column<3;++column) for(int row=0;row<3;++row) out[column*3+row]=GLfloat(m[row][column]);
    offset[0]=GLfloat(fullRange ? 0 : 16.0/255);offset[1]=offset[2]=GLfloat(128.0/255);
}
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
    glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maxTextureSize_);
}
VideoPresenter::~VideoPresenter() {for(auto t:textures_) if(t) glDeleteTextures(1,&t);}
void VideoPresenter::resize(int w,int h) {if(w>0 && h>0) {width_=w;height_=h;}}
void VideoPresenter::upload(const VideoFrame& frame) {
    const bool planar=frame.format==VideoPixels::Yuv420;
    const int chromaWidth=(frame.width+1)/2,chromaHeight=(frame.height+1)/2;
    const bool layoutValid=planar
        ? frame.stride>=frame.width && frame.chromaStride>=chromaWidth &&
          size_t(frame.stride)*size_t(frame.height)+2*size_t(frame.chromaStride)*size_t(chromaHeight)<=frame.pixels.size()
        : frame.stride>=frame.width*4 && frame.stride%4==0 && size_t(frame.stride)*size_t(frame.height)<=frame.pixels.size();
    if(frame.width<=0 || frame.height<=0 || frame.width>1920 || frame.height>1920 ||
       frame.width>maxTextureSize_ || frame.height>maxTextureSize_ || !layoutValid ||
       !std::isfinite(frame.pixelAspect) || frame.pixelAspect<=0)
        throw std::runtime_error("Invalid video frame dimensions or stride");
    struct Plane {int width,height,rowLength;const uint8_t* data;};
    const size_t lumaBytes=size_t(frame.stride)*frame.height,chromaBytes=size_t(frame.chromaStride)*chromaHeight;
    std::array<Plane,3> planes{};
    int count=1;
    if(planar) {
        planes={Plane{frame.width,frame.height,frame.stride,frame.pixels.data()},
                Plane{chromaWidth,chromaHeight,frame.chromaStride,frame.pixels.data()+lumaBytes},
                Plane{chromaWidth,chromaHeight,frame.chromaStride,frame.pixels.data()+lumaBytes+chromaBytes}};
        count=3;
    } else planes[0]={frame.width,frame.height,frame.stride/4,frame.pixels.data()};
    const GLenum internal=planar ? GL_R8 : GL_RGBA8, format=planar ? GL_RED : GL_RGBA;
    {
    fluid_gl::StateGuard state;UnpackGuard unpack;
    glActiveTexture(GL_TEXTURE0);glBindSampler(0,0);
    if(textures_[0] && planar==planar_ && frame.width==textureWidth_ && frame.height==textureHeight_) {
        // Same layout: overwrite the existing textures instead of allocating new ones each
        // frame. A failed upload raises a GL error and leaves the old picture in place.
        for(int n=0;n<count;++n) {
            glBindTexture(GL_TEXTURE_2D,textures_[n]);glPixelStorei(GL_UNPACK_ROW_LENGTH,planes[n].rowLength);
            glTexSubImage2D(GL_TEXTURE_2D,0,0,0,planes[n].width,planes[n].height,format,GL_UNSIGNED_BYTE,planes[n].data);
            if(glGetError()!=GL_NO_ERROR) throw std::runtime_error("Video texture upload failed");
        }
    } else {
        std::array<GLuint,3> candidates{};
        bool ok=true;
        for(int n=0;n<count && ok;++n) {
            glGenTextures(1,&candidates[n]);glBindTexture(GL_TEXTURE_2D,candidates[n]);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ROW_LENGTH,planes[n].rowLength);
            glTexImage2D(GL_TEXTURE_2D,0,internal,planes[n].width,planes[n].height,0,format,GL_UNSIGNED_BYTE,planes[n].data);
            ok=candidates[n] && glGetError()==GL_NO_ERROR;
        }
        if(!ok) {
            for(auto& t:candidates) if(t) glDeleteTextures(1,&t);
            throw std::runtime_error("Video texture upload failed");
        }
        for(auto& t:textures_) if(t) glDeleteTextures(1,&t);
        textures_=candidates;textureWidth_=frame.width;textureHeight_=frame.height;planar_=planar;
    }
    }
    yuvTransform(frame.matrix,frame.fullRange,yuvMatrix_.data(),yuvOffset_.data());
    rotation_=frame.rotationDegrees;
    aspect_=double(frame.width)*frame.pixelAspect/frame.height;
    if(rotation_==90 || rotation_==270) aspect_=1.0/aspect_;
}
void VideoPresenter::render() {
    fluid_gl::resetState();glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,width_,height_);
    const GLfloat black[]={0,0,0,1};glClearBufferfv(GL_COLOR,0,black);
    if(!textures_[0]) return;
    int w=width_,h=height_;
    if(double(w)/h>aspect_) w=std::max(1,int(h*aspect_));else h=std::max(1,int(w/aspect_));
    glViewport((width_-w)/2,(height_-h)/2,w,h);
    program_.use();vao_.bind();program_.texture("image",textures_[0],0);glUniform1i(program_.location("rotation"),rotation_);
    glUniform1i(program_.location("planar"),planar_ ? 1 : 0);
    if(planar_) {
        program_.texture("chromaU",textures_[1],1);program_.texture("chromaV",textures_[2],2);
        glUniformMatrix3fv(program_.location("yuvToRgb"),1,GL_FALSE,yuvMatrix_.data());
        glUniform3fv(program_.location("yuvOffset"),1,yuvOffset_.data());
    }
    glDrawArrays(GL_TRIANGLES,0,3);
    if(glGetError()!=GL_NO_ERROR) throw std::runtime_error("Video rendering failed");
}
}
