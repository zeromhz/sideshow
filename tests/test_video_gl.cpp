#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "video_presenter.h"
#include "video_backend.h"
#include "brightness_pass.h"
#include "support/video_fixtures.h"
#include "support/video_tree.h"
#include "fluid_backend.h"
#include "visual_controller.h"
#include "gl.h"
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace sideshow;
void require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
std::array<unsigned char,4> pixel(int x,int y) {
    std::array<unsigned char,4> p{};glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p.data());return p;
}
int main() {
    SDL_SetMainReady();if(SDL_Init(SDL_INIT_VIDEO)) return 1;
#ifdef SIDESHOW_GLES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
#endif
    auto window=SDL_CreateWindow("Video tests",0,0,320,180,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    auto context=window?SDL_GL_CreateContext(window):nullptr;
    if(!context) {std::cerr<<SDL_GetError();SDL_Quit();return 1;}
    int status=0;
    try {
        VideoPresenter presenter(320,180);VideoFrame frame;frame.width=frame.height=2;frame.stride=8;
        frame.rgba={255,0,0,255,255,0,0,255,255,0,0,255,255,0,0,255};
        GLuint pbo=0,sampler=0;glGenBuffers(1,&pbo);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);
        glBufferData(GL_PIXEL_UNPACK_BUFFER,64,nullptr,GL_STATIC_DRAW);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,7);glPixelStorei(GL_UNPACK_SKIP_ROWS,3);glPixelStorei(GL_UNPACK_ALIGNMENT,8);
        glGenSamplers(1,&sampler);glSamplerParameteri(sampler,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);glBindSampler(0,sampler);
        glActiveTexture(GL_TEXTURE7);presenter.upload(frame);
        GLint binding=0;glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&binding);require(binding==int(pbo),"Upload did not restore PBO");
        glGetIntegerv(GL_ACTIVE_TEXTURE,&binding);require(binding==GL_TEXTURE7,"Upload did not restore texture unit");
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_ALIGNMENT,4);
        glEnable(GL_SCISSOR_TEST);glScissor(0,0,1,1);glEnable(GL_DEPTH_TEST);glColorMask(false,false,false,false);
        presenter.render();require(pixel(0,90)[0]==0,"Missing letterbox");require(pixel(160,90)[0]>250,"Video invisible with inherited state");
        const auto beforeFailure=fluid_gl_tracking::live;
        video_gl_tracking::failNextUpload=true;
        bool allocationFailed=false;try{presenter.upload(frame);}catch(...){allocationFailed=true;}
        require(allocationFailed,"Injected upload failure not observed");
        require(fluid_gl_tracking::live==beforeFailure,"Failed upload leaked a GL resource");
        presenter.render();require(pixel(160,90)[0]>250,"Allocation failure replaced old picture");
        frame.stride=1;bool failed=false;try{presenter.upload(frame);}catch(...){failed=true;}
        require(failed,"Invalid stride accepted");presenter.render();require(pixel(160,90)[0]>250,"Failed upload lost old picture");
        frame.stride=8;frame.rgba={255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255};frame.rotationDegrees=90;
        presenter.upload(frame);require(glGetError()==GL_NO_ERROR,"Rotation upload left GL error");presenter.render();require(pixel(100,30)[0]>200,"Rotation quadrant incorrect");
        presenter.resize(0,0);presenter.resize(320,180);
        BrightnessPass brightness;brightness.apply(0,320,180);require(pixel(160,90)[0]==0,"Blackout failed");
        glDeleteBuffers(1,&pbo);glDeleteSamplers(1,&sampler);
        VideoWorker worker(videoFixture(""));Config config;config.video.preset="reordered.mp4";
        BackendInit init{config,{}, {".","reordered.mp4"},320,180};
        VideoBackend video(init,worker);video.setPaused(true);
        auto wait=[&] {
            auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
            while(video.preparation().state==PreparationState::Pending && std::chrono::steady_clock::now()<deadline) {
                video.maintenance();std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            require(video.preparation().state==PreparationState::Ready,"Video did not prepare");
        };
        wait();video.render(.1);auto before=pixel(160,90);
        video.render(10);require(pixel(160,90)==before,"Paused video moved");
        require(video.selectPreset("corrupt.mp4"),"Catalog did not accept corrupt candidate");
        for(int i=0;i<50;++i) {video.maintenance();video.render(.01);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        require(video.selection().preset=="reordered.mp4","Bad clip replaced selection");
        require(pixel(160,90)==before,"Bad clip replaced picture");
        video.selectPreset("bars-audio.avi");
        require(!video.selectPreset("missing.mp4"),"Unknown clip accepted");
        for(int i=0;i<50;++i) {video.maintenance();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        require(video.selection().preset=="reordered.mp4","Unknown clip did not supersede pending selection");
        video.selectPreset("bars-audio.avi");
        require(!video.selectPlaylist("missing folder"),"Unknown folder accepted");
        for(int i=0;i<50;++i) {video.maintenance();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        require(video.selection().preset=="reordered.mp4","Unknown folder did not supersede pending selection");
        const auto beforeCandidate=fluid_gl_tracking::live;
        video_gl_tracking::failNextUpload=true;video.selectPreset("bars-audio.avi");
        for(int i=0;i<50;++i) {video.maintenance();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        require(!video_gl_tracking::failNextUpload,"Candidate upload did not run");
        require(fluid_gl_tracking::live==beforeCandidate,"Candidate failure leaked handles");
        video.render(1);require(pixel(160,90)==before,"Candidate failure replaced picture");
        require(video.selection().preset=="reordered.mp4","Candidate failure replaced selection");
        video.restart();for(int i=0;i<50;++i) {video.maintenance();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        video.render(1);require(pixel(160,90)==before,"Paused restart changed first frame");
        video.setEnd(VideoEnd::Hold);video.setPaused(false);video.render(10);
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        auto uploadsBefore=video_gl_tracking::uploads;video.maintenance();
        require(video_gl_tracking::uploads-uploadsBefore<=1,"Late frames caused multiple uploads in one maintenance pass");
        require(glGetError()==GL_NO_ERROR,"GL error");
        VideoTree tree;tree.file("nested folder/first.mp4");tree.file("nested folder/second.mp4");
        std::filesystem::copy_file(videoFixture("reordered.mp4"),tree.root/"nested folder/first.mp4",std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file(videoFixture("reordered.mp4"),tree.root/"nested folder/second.mp4",std::filesystem::copy_options::overwrite_existing);
        VideoWorker liveWorker(tree.root);Config liveConfig;liveConfig.visuals.mode=VisualMode::Fluid;
        liveConfig.fluid.simulationResolution=32;liveConfig.fluid.dyeResolution=128;
        VisualController controller(liveConfig,320,180,[&](VisualMode mode,const BackendInit& i)->std::unique_ptr<VisualBackend>{
            if(mode==VisualMode::Video) return std::make_unique<VideoBackend>(i,liveWorker);
            return std::make_unique<FluidBackend>(i);
        });
        auto pump=[&](int count) {for(int i=0;i<count;++i){controller.maintenance();controller.render(.05);std::this_thread::sleep_for(std::chrono::milliseconds(2));}};
        controller.apply(cmd::Mode{VisualMode::Video});controller.apply(cmd::Preset{"nested folder/first.mp4"});controller.apply(cmd::VideoPause{true});
        pump(100);require(controller.statusPreset()=="video/nested folder/first.mp4","Pending mode/preset routing failed");
        controller.apply(cmd::VideoEndMode{VideoEnd::Next});pump(30);
        require(controller.statusPreset()=="video/nested folder/first.mp4","Paused next advanced");
        controller.apply(cmd::VideoPause{false});
        bool advanced=false;
        for(int i=0;i<200 && !advanced;++i) {pump(1);advanced=controller.statusPreset()=="video/nested folder/second.mp4";}
        require(advanced,"EOF next did not advance");
        controller.apply(cmd::VideoEndMode{VideoEnd::Hold});pump(100);
        require(controller.statusPreset()=="video/nested folder/second.mp4","Hold advanced");
        controller.apply(cmd::VideoEndMode{VideoEnd::Next});controller.apply(cmd::Lock{true});pump(100);
        require(controller.statusPreset()=="video/nested folder/second.mp4","Locked next changed clip");
        controller.apply(cmd::Mode{VisualMode::Fluid});pump(2);controller.apply(cmd::Mode{VisualMode::Video});pump(100);
        require(controller.statusPreset()=="video/nested folder/second.mp4","Returning video lost selection");
        for(int i=0;i<10;++i) {controller.apply(cmd::Mode{VisualMode::Fluid});controller.apply(cmd::Mode{VisualMode::Video});}
        pump(100);require(controller.mode()==VisualMode::Video,"Rapid mode changes stalled");
        std::cout<<"Video GL checks passed\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';status=1;}
    SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();return status;
}
