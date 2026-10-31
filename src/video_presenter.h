#pragma once
#include <array>
#include "video_types.h"
#include "fluid_gl.h"
namespace sideshow {
class VideoPresenter {
public:
    VideoPresenter(int drawableWidth,int drawableHeight);
    ~VideoPresenter();
    void upload(const VideoFrame&);
    void resize(int width,int height);
    void render();
private:
    fluid_gl::Program program_;
    fluid_gl::VertexArray vao_;
    std::array<GLuint,3> textures_{};  // RGBA: [0]; YUV 4:2:0: Y, U, V planes
    bool planar_=false;
    int textureWidth_=0,textureHeight_=0;
    GLint maxTextureSize_=0;
    std::array<GLfloat,9> yuvMatrix_{};
    std::array<GLfloat,3> yuvOffset_{};
    int width_,height_,rotation_=0;
    double aspect_=1;
};
}
