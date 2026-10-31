#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "fluid_renderer.h"
#include "fluid_backend.h"
#include "milkdrop_backend.h"
#include "renderer.h"
#include "visual_controller.h"
#include "brightness_pass.h"
#include "fluid_motion.h"
#include "gl.h"
#include "fluid_shaders.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
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
        // A previous backend may leave any active unit, texture and sampler bound.
        GLuint priorTextures[2]={},inheritedSampler=0;
        glGenTextures(2,priorTextures);glGenSamplers(1,&inheritedSampler);
        glSamplerParameteri(inheritedSampler,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,priorTextures[0]);
        glBindSampler(0,inheritedSampler);
        glActiveTexture(GL_TEXTURE7);glBindTexture(GL_TEXTURE_2D,priorTextures[1]);
        const auto beforeProbe=fluid_gl_tracking::live;
        fluid_gl_tracking::failCompileAfter=0;
        bool probeFailed=false;
        try{sideshow::fluid_gl::probeCapabilities();}catch(const std::exception&){probeFailed=true;}
        require(probeFailed,"Injected probe shader failure was not observed");
        require(fluid_gl_tracking::live==beforeProbe,"Failed probe leaked GL handles");
        GLint failedActive=0,failedBinding=0;
        glGetIntegerv(GL_ACTIVE_TEXTURE,&failedActive);glGetIntegerv(GL_TEXTURE_BINDING_2D,&failedBinding);
        require(failedActive==GL_TEXTURE7&&failedBinding==static_cast<GLint>(priorTextures[1]),"Throwing probe corrupted retained texture state");
        auto caps=sideshow::fluid_gl::probeCapabilities();
        GLint active=0,binding=0,sampler=0;
        glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
        require(active==GL_TEXTURE7,"Probe did not restore active texture unit");
        glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
        require(binding==static_cast<GLint>(priorTextures[1]),"Probe changed entry-unit texture");
        glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
        glGetIntegerv(GL_SAMPLER_BINDING,&sampler);
        require(binding==static_cast<GLint>(priorTextures[0]),"Probe changed unit-zero texture");
        require(sampler==static_cast<GLint>(inheritedSampler),"Probe changed unit-zero sampler");
        // Fail after shaders, VAO and noise target have been allocated.
        glActiveTexture(GL_TEXTURE7);
        const auto beforeCandidate=fluid_gl_tracking::live;
        bool candidateFailed=false;
        try{sideshow::FluidRenderer failed(2147483647,1,config);}catch(const std::exception&){candidateFailed=true;}
        require(candidateFailed,"Oversized candidate initialized");
        require(fluid_gl_tracking::live==beforeCandidate,"Failed candidate leaked GL handles");
        glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
        require(active==GL_TEXTURE7&&binding==static_cast<GLint>(priorTextures[1]),"Failed candidate changed retained renderer bindings");
        glActiveTexture(GL_TEXTURE0);
        glDeleteTextures(2,priorTextures);
        for(int unit=0;unit<4;++unit)glBindSampler(unit,inheritedSampler);
        std::cout << "float filtering: " << caps.linear << "\n";
        caps.scalar=GL_RGBA16F;caps.vector=GL_RGBA16F;caps.linear=false;
        for(int i=0;i<3;++i) {
            sideshow::FluidRenderer conservative(320,180,config,caps);
            conservative.render(.016f,*sideshow::findFluidPreset("lava"),{{.5f,.5f,100,100,{1,0,0}}});
            require(glGetError()==GL_NO_ERROR,"Manual-filter/RGBA fallback failed");
        }
        bool zeroSizeFailed=false;
        try{sideshow::FluidRenderer zeroSize(0,0,config);}catch(const std::exception&){zeroSizeFailed=true;}
        require(zeroSizeFailed,"Zero-size startup created an unusable renderer");
        // Equal simulation time and identical splats: persistence changes only fading.
        auto dyeEnergy = [&](float persistence) {
            sideshow::FluidRenderer r(320,180,config);
            auto p=*sideshow::findFluidPreset("lava");
            p.bloom=false;p.sunrays=false;p.shading=false;
            r.render(1.0f/60,p,{{.5f,.5f,0,0,{.2f,0,0}}},persistence);
            for(int frame=0;frame<120;++frame) r.render(1.0f/60,p,{},persistence);
            std::vector<unsigned char> pixels(320*180*4);
            glReadPixels(0,0,320,180,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            size_t energy=0;for(size_t i=0;i<pixels.size();i+=4)energy+=pixels[i];
            return energy;
        };
        const auto normal=dyeEnergy(1), lingering=dyeEnergy(4);
        require(normal>0 && lingering>normal*1.3,"Persistence did not prolong visible dye");
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
        {
            sideshow::FluidRenderer preview(320,180,config);
            sideshow::FluidMotion seeded(42);
            const auto& p=*sideshow::findFluidPreset("pompeii");
            preview.render(1.0f/60,p,seeded.seed(p),4);
            for(int frame=0;frame<120;++frame)
                preview.render(1.0f/60,p,seeded.update(1.0f/60,0,2.5f,p),4);
            std::vector<unsigned char> pixels(320*180*4);
            glReadPixels(0,0,320,180,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            size_t red=0,green=0,blue=0;
            for(size_t i=0;i<pixels.size();i+=4){red+=pixels[i];green+=pixels[i+1];blue+=pixels[i+2];}
            require(red>1000 && blue>1000 && green<(red+blue)/20,"Pompeii lost its duotone palette");
            std::ofstream image("/tmp/sideshow-fluid-pompeii.ppm",std::ios::binary);
            image << "P6\n320 180\n255\n";
            for(int y=179;y>=0;--y)for(int x=0;x<320;++x)
                image.write(reinterpret_cast<const char*>(pixels.data()+(y*320+x)*4),3);
        }
        bool resizeFailed=false;
        try{renderer.resize(2147483647,1);}catch(const std::exception&){resizeFailed=true;}
        require(resizeFailed,"Oversized target was accepted");
        renderer.render(.016f,*sideshow::findFluidPreset("lava"),{});
        for(int unit=0;unit<4;++unit)glBindSampler(unit,0);
        glDeleteSamplers(1,&inheritedSampler);
        renderer.resize(0,0); renderer.resize(180,320); renderer.resize(320,180);
        renderer.render(100,*sideshow::findFluidPreset("lava"),{});
        require(glGetError()==GL_NO_ERROR,"GL error after resizing");
        sideshow::Config app;app.visuals.mode=sideshow::VisualMode::Fluid;app.fluid=config;
        app.presets.root="/nonexistent";
        bool reject=false;
        sideshow::VisualController controller(app,320,180,[&](sideshow::VisualMode m,const sideshow::BackendInit& i)->std::unique_ptr<sideshow::VisualBackend>{
            if(reject) {
                auto invalid=i;invalid.width=2147483647;invalid.height=1;
                return std::make_unique<sideshow::FluidBackend>(invalid);
            }
            if(m==sideshow::VisualMode::Fluid)return std::make_unique<sideshow::FluidBackend>(i);
            return std::make_unique<sideshow::MilkdropBackend>(i);
        });
        sideshow::BrightnessPass output;
        controller.apply(sideshow::cmd::Preset{"lava"});
        controller.apply(sideshow::cmd::Lock{true});
        controller.apply(sideshow::cmd::Playlist{"missing"});
        require(controller.statusPreset()=="fluid/lava","Fluid playlist rejection changed preset");
        for(int i=0;i<3;++i) {
            for(auto mode:{sideshow::VisualMode::Milkdrop,sideshow::VisualMode::Fluid}) {
                require(controller.setMode(mode),"Mode switch failed");
                controller.render(.016f);output.apply(0,320,180);
                std::vector<unsigned char> black(320*180*4);glReadPixels(0,0,320,180,GL_RGBA,GL_UNSIGNED_BYTE,black.data());
                for(size_t j=0;j<black.size();j+=4)require(black[j]==0&&black[j+1]==0&&black[j+2]==0,"Blackout flashed during mode switch");
                require(glGetError()==GL_NO_ERROR,"GL error during mode switch");
            }
        }
        require(controller.statusPreset()=="fluid/lava","Preset not restored on return");
        require(controller.setMode(sideshow::VisualMode::Milkdrop),"Could not prepare retained backend");
        reject=true;require(!controller.setMode(sideshow::VisualMode::Fluid),"Partially allocated candidate accepted");
        require(controller.mode()==sideshow::VisualMode::Milkdrop,"Failed fluid candidate replaced retained backend");
        controller.render(.016f);require(glGetError()==GL_NO_ERROR,"Retained renderer failed after partial candidate allocation");
        reject=false;require(controller.setMode(sideshow::VisualMode::Fluid),"Fluid did not initialize after failure");
        controller.render(.016f);output.apply(1,320,180);
        require(controller.statusPreset()=="fluid/lava","Failed switch changed active backend");
        std::vector<unsigned char> lit(320*180*4);glReadPixels(0,0,320,180,GL_RGBA,GL_UNSIGNED_BYTE,lit.data());
        size_t sum=0;for(size_t i=0;i<lit.size();i+=4)sum+=lit[i]+lit[i+1]+lit[i+2];
        require(sum>1000,"Fluid did not return after blackout");
        {
            // MilkDrop render scale: a preset whose composite paints a uv gradient must fill the
            // whole screen when rendered at half size, and match the full-size render.
            const char* path="/tmp/sideshow_gradient_test.milk";
            {std::ofstream milk(path);milk<<"MILKDROP_PRESET_VERSION=201\nPSVERSION=2\nPSVERSION_WARP=2\nPSVERSION_COMP=2\n[preset00]\n"
                "comp_1=`shader_body\ncomp_2=`{\ncomp_3=`ret = float3(uv.x, uv.y, 0.5);\ncomp_4=`}\n";}
            auto capture=[&](double scale,int w,int h,sideshow::RenderSize& internal){
                sideshow::Config::Presets presets;presets.renderScale=scale;presets.textures="/nonexistent";
                sideshow::Renderer milkdrop(w,h,30,presets);
                milkdrop.renderFrame();  // projectM finishes initializing on its first frame
                projectm_load_preset_file(milkdrop.handle(),path,false);
                for(int i=0;i<5;++i) milkdrop.renderFrame();
                internal=milkdrop.internalSize();
                std::vector<unsigned char> px(size_t(w)*h*4);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,px.data());
                require(glGetError()==GL_NO_ERROR,"GL error during scaled MilkDrop render");
                return px;};
            sideshow::RenderSize full{},half{};
            auto scaled=capture(0.5,320,180,half);auto direct=capture(1.0,320,180,full);
            require(full.width==320&&full.height==180,"Scale 1 did not render at screen size");
            require(half.width==160&&half.height==90,"Scale 0.5 did not halve the render size");
            auto at=[](const std::vector<unsigned char>& px,int x,int y,int c){return int(px[(size_t(y)*320+x)*4+c]);};
            // The gradient must span the full screen: red runs across, green runs up or down.
            require(std::abs(at(scaled,316,90,0)-at(scaled,3,90,0))>200,"Scaled frame does not span the screen width");
            require(std::abs(at(scaled,160,176,1)-at(scaled,160,3,1))>150,"Scaled frame does not span the screen height");
            int worst=0;
            for(int y=4;y<180;y+=16)for(int x=4;x<320;x+=16)for(int c=0;c<3;++c)
                worst=std::max(worst,std::abs(at(scaled,x,y,c)-at(direct,x,y,c)));
            require(worst<=24,"Scaled frame differs from the full-size render");
            // Resizing to another aspect ratio keeps the scale and still fills the screen.
            sideshow::Config::Presets presets;presets.renderScale=2.0/3.0;presets.textures="/nonexistent";
            sideshow::Renderer milkdrop(320,180,30,presets);
            milkdrop.resize(180,320);
            require(milkdrop.internalSize().width==120&&milkdrop.internalSize().height==213,"Resize lost the render scale");
            milkdrop.resize(320,180);milkdrop.renderFrame();
            require(milkdrop.internalSize().width==213&&milkdrop.internalSize().height==120,"Resize back lost the render scale");
            require(glGetError()==GL_NO_ERROR,"GL error after scaled MilkDrop resize");
            std::remove(path);
        }
    } catch(const std::exception& e) {std::cerr << e.what() << '\n';result=1;}
    SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();return result;
}
