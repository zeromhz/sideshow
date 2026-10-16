#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "fluid_renderer.h"
#include "fluid_motion.h"
#include "gl.h"
#include "fluid_shaders.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
void require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
int main() {
    SDL_SetMainReady();
    if(SDL_Init(SDL_INIT_VIDEO)!=0) {std::cerr << "GL prerequisite unavailable: " << SDL_GetError(); return 1;}
#ifdef SIDESHOW_GLES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
#endif
    auto* window=SDL_CreateWindow("Fluid integration",0,0,320,180,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    auto context=window ? SDL_GL_CreateContext(window) : nullptr;
    if(!context) {std::cerr << "GL prerequisite unavailable: " << SDL_GetError(); SDL_Quit(); return 1;}
    int result=0;
    try {
        sideshow::Config::Fluid config; config.simulationResolution=32;config.dyeResolution=128;
        bool shaderFailed=false;
        try {sideshow::fluid_gl::Program bad(sideshow::fluid_shader::vertex,"invalid shader");}
        catch(const std::exception&) {shaderFailed=true;}
        require(shaderFailed,"Invalid shader was accepted");
        GLuint texture=0,fbo=0;
        {sideshow::fluid_gl::Target t(8,8,GL_RGBA16F,GL_RGBA,false);texture=t.texture;fbo=t.fbo;}
        require(!glIsTexture(texture)&&!glIsFramebuffer(fbo),"Target handles leaked");
        auto caps=sideshow::fluid_gl::probeCapabilities();
        std::cout << "float filtering: " << caps.linear << "\n";
        caps.scalar=GL_RGBA16F;caps.vector=GL_RGBA16F;caps.linear=false;
        for(int i=0;i<3;++i) {
            sideshow::FluidRenderer conservative(320,180,config,caps);
            conservative.render(.016f,*sideshow::findFluidPreset("lava"),{{.5f,.5f,100,100,{1,0,0}}});
            require(glGetError()==GL_NO_ERROR,"Manual-filter/RGBA fallback failed");
        }
        sideshow::FluidRenderer renderer(320,180,config);
        sideshow::FluidMotion motion(7);
        for(const auto& p:sideshow::fluidPresets()) {
            auto splats=motion.seed(p);
            renderer.render(1.0f/60,p,splats);
            for(int i=0;i<3;++i) renderer.render(1.0f/60,p,{});
            std::vector<unsigned char> pixels(320*180*4);
            glReadPixels(0,0,320,180,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            size_t total=0;for(size_t i=0;i<pixels.size();i+=4)total+=pixels[i]+pixels[i+1]+pixels[i+2];
            require(total>1000,"Empty fluid output");
            require(glGetError()==GL_NO_ERROR,"GL error after fluid rendering");
            if(p.slug=="lava") {
                std::ofstream image("/tmp/sideshow-fluid-lava.ppm",std::ios::binary);
                image << "P6\n320 180\n255\n";
                for(int y=179;y>=0;--y)for(int x=0;x<320;++x)
                    image.write(reinterpret_cast<const char*>(pixels.data()+(y*320+x)*4),3);
            }
            std::cout << p.slug << " rendered\n";
        }
        bool resizeFailed=false;
        try{renderer.resize(2147483647,1);}catch(const std::exception&){resizeFailed=true;}
        require(resizeFailed,"Oversized target was accepted");
        renderer.render(.016f,*sideshow::findFluidPreset("lava"),{});
        renderer.resize(0,0); renderer.resize(180,320); renderer.resize(320,180);
        renderer.render(100,*sideshow::findFluidPreset("lava"),{});
        require(glGetError()==GL_NO_ERROR,"GL error after resizing");
    } catch(const std::exception& e) {std::cerr << e.what() << '\n';result=1;}
    SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();return result;
}
