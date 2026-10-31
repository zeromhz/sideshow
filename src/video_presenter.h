#pragma once
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
    GLuint texture_=0;
    int textureWidth_=0,textureHeight_=0;
    int width_,height_,rotation_=0;
    double aspect_=1;
};
}
