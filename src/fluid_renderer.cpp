// Native adaptation of Pavel Dobryakov's MIT fluid solver. See third_party/fluid.
#include "fluid_renderer.h"
#include "fluid_shaders.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
namespace sideshow {
using fluid_gl::Target;
using fluid_gl::Program;
namespace {
struct Pair {
    Target read,write;
    Pair(int w,int h,GLenum format,bool linear)
        :read(w,h,format,fluid_gl::externalFormat(format),linear),write(w,h,format,fluid_gl::externalFormat(format),linear){}
    void swap(){std::swap(read,write);}
};
std::pair<int,int> dimensions(int resolution,int w,int h) {
    if(w<=0||h<=0)throw std::runtime_error("Fluid requires a positive drawable size");
    const double ratio=static_cast<double>(std::max(w,h))/std::min(w,h);
    GLint limit=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&limit);
    const double longer=std::round(resolution*ratio);
    if(longer>limit)throw std::runtime_error("Fluid aspect ratio exceeds texture size limit");
    return w>=h?std::pair<int,int>{static_cast<int>(longer),resolution}:std::pair<int,int>{resolution,static_cast<int>(longer)};
}
struct Fields {
    Pair velocity,dye,pressure;
    Target curl,divergence,bloom,sunrays,sunTemp;
    std::vector<Target> bloomLevels;
    Fields(std::pair<int,int> sim,std::pair<int,int> dyeSize,std::pair<int,int> bloomSize,
           std::pair<int,int> rays,fluid_gl::Capabilities caps)
        :velocity(sim.first,sim.second,caps.vector,caps.linear),
         dye(dyeSize.first,dyeSize.second,GL_RGBA16F,caps.linear),
         pressure(sim.first,sim.second,caps.scalar,false),
         curl(sim.first,sim.second,caps.scalar,fluid_gl::externalFormat(caps.scalar),false),
         divergence(sim.first,sim.second,caps.scalar,fluid_gl::externalFormat(caps.scalar),false),
         bloom(bloomSize.first,bloomSize.second,GL_RGBA16F,GL_RGBA,caps.linear),
         sunrays(rays.first,rays.second,caps.scalar,fluid_gl::externalFormat(caps.scalar),caps.linear),
         sunTemp(rays.first,rays.second,caps.scalar,fluid_gl::externalFormat(caps.scalar),caps.linear) {
        int w=bloomSize.first,h=bloomSize.second;
        for(int i=0;i<8;++i){w/=2;h/=2;if(w<2||h<2)break;bloomLevels.emplace_back(w,h,GL_RGBA16F,GL_RGBA,caps.linear);}
    }
};
}
struct FluidRenderer::Impl {
    Config::Fluid config;
    fluid_gl::Capabilities caps;
    int width=0,height=0;
    fluid_gl::VertexArray vao;
    Program clear{fluid_shader::vertex,fluid_shader::clear};
    Program splat{fluid_shader::vertex,fluid_shader::splat};
    Program curl{fluid_shader::vertex,fluid_shader::curl};
    Program vorticity{fluid_shader::vertex,fluid_shader::vorticity};
    Program divergence{fluid_shader::vertex,fluid_shader::divergence};
    Program pressure{fluid_shader::vertex,fluid_shader::pressure};
    Program gradient{fluid_shader::vertex,fluid_shader::gradientSubtract};
    Program advection;
    Program prefilter{fluid_shader::vertex,fluid_shader::bloomPrefilter};
    Program bloomBlur{fluid_shader::vertex,fluid_shader::bloomBlur};
    Program bloomFinal{fluid_shader::vertex,fluid_shader::bloomFinal};
    Program mask{fluid_shader::vertex,fluid_shader::sunraysMask};
    Program rays{fluid_shader::vertex,fluid_shader::sunrays};
    Program blur{fluid_shader::blurVertex,fluid_shader::blur};
    std::array<std::unique_ptr<Program>,8> displays;
    Target noise{8,8,GL_RGBA16F,GL_RGBA,false};
    std::unique_ptr<Fields> fields;
    Impl(int w,int h,const Config::Fluid& c,fluid_gl::Capabilities cap)
        :config(c),caps(cap),advection(fluid_shader::vertex,fluid_shader::advection,cap.linear?"":"#define MANUAL_FILTERING\n") {
        if(w<=0||h<=0)throw std::runtime_error("Fluid requires a positive initial drawable size");
        if(c.simulationResolution<32||c.simulationResolution>256||c.dyeResolution<128||c.dyeResolution>1024)
            throw std::runtime_error("Fluid quality outside supported bounds");
        for(size_t i=0;i<displays.size();++i) {
            std::string defines;
            if(i&1)defines+="#define SHADING\n";
            if(i&2)defines+="#define BLOOM\n";
            if(i&4)defines+="#define SUNRAYS\n";
            displays[i]=std::make_unique<Program>(fluid_shader::vertex,fluid_shader::display,defines);
        }
        std::array<float,8*8*4> pixels{};
        std::mt19937 rng(17);for(auto& p:pixels)p=static_cast<float>(rng()%256)/255;
        glBindTexture(GL_TEXTURE_2D,noise.texture);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,8,8,GL_RGBA,GL_FLOAT,pixels.data());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
        resize(w,h);
    }
    void resize(int w,int h) {
        if(w<=0||h<=0|| (w==width&&h==height))return;
        fluid_gl::resetState();
        auto replacement=std::make_unique<Fields>(dimensions(config.simulationResolution,w,h),dimensions(config.dyeResolution,w,h),dimensions(256,w,h),dimensions(196,w,h),caps);
        fields.swap(replacement);width=w;height=h;
    }
    void begin(Program& p,const Target& source) {
        fluid_gl::resetState();vao.bind();p.use();p.two("texelSize",1.0f/source.width,1.0f/source.height);
    }
    void draw(const Target& dest) {
        glBindFramebuffer(GL_FRAMEBUFFER,dest.fbo);glViewport(0,0,dest.width,dest.height);glDrawArrays(GL_TRIANGLES,0,3);
    }
    void render(float dt,const FluidPreset& p,const std::vector<FluidSplat>& splats,float persistence) {
        auto& f=*fields;
        dt=std::isfinite(dt)?std::clamp(dt,0.0f,1.0f/60):0;
        const float aspect=static_cast<float>(width)/height;
        for(const auto& s:splats) {
            begin(splat,f.velocity.read);splat.one("aspectRatio",aspect);splat.two("point",s.x,s.y);
            splat.one("radius",p.splatRadius/100*std::max(1.0f,aspect));
            splat.texture("uTarget",f.velocity.read.texture,0);
            splat.three("color",s.dx*(aspect<1?aspect:1),s.dy/(aspect>1?aspect:1),0);
            draw(f.velocity.write);f.velocity.swap();
            splat.texture("uTarget",f.dye.read.texture,0);
            // Reference autonomous splats multiply the 0.15 palette color by ten.
            splat.three("color",s.color.r*1.5f,s.color.g*1.5f,s.color.b*1.5f);
            draw(f.dye.write);f.dye.swap();
        }
        begin(curl,f.velocity.read);curl.texture("uVelocity",f.velocity.read.texture,0);draw(f.curl);
        begin(vorticity,f.velocity.read);vorticity.texture("uVelocity",f.velocity.read.texture,0);vorticity.texture("uCurl",f.curl.texture,1);
        vorticity.one("curl",p.curl);vorticity.one("dt",dt);draw(f.velocity.write);f.velocity.swap();
        begin(divergence,f.velocity.read);divergence.texture("uVelocity",f.velocity.read.texture,0);draw(f.divergence);
        begin(clear,f.pressure.read);clear.texture("uTexture",f.pressure.read.texture,0);clear.one("value",p.pressure);draw(f.pressure.write);f.pressure.swap();
        begin(pressure,f.velocity.read);pressure.texture("uDivergence",f.divergence.texture,0);
        for(int i=0;i<p.pressureIterations;++i){pressure.texture("uPressure",f.pressure.read.texture,1);draw(f.pressure.write);f.pressure.swap();}
        begin(gradient,f.velocity.read);gradient.texture("uPressure",f.pressure.read.texture,0);gradient.texture("uVelocity",f.velocity.read.texture,1);draw(f.velocity.write);f.velocity.swap();
        begin(advection,f.velocity.read);advection.texture("uVelocity",f.velocity.read.texture,0);advection.texture("uSource",f.velocity.read.texture,0);
        advection.two("dyeTexelSize",1.0f/f.velocity.read.width,1.0f/f.velocity.read.height);advection.one("dt",dt);advection.one("dissipation",p.velocityDissipation);draw(f.velocity.write);f.velocity.swap();
        advection.texture("uVelocity",f.velocity.read.texture,0);advection.texture("uSource",f.dye.read.texture,1);
        advection.two("dyeTexelSize",1.0f/f.dye.read.width,1.0f/f.dye.read.height);advection.one("dissipation",p.densityDissipation / (std::isfinite(persistence) ? std::clamp(persistence,1.0f,10.0f) : 1.0f));draw(f.dye.write);f.dye.swap();
        if(p.bloom) {
            begin(prefilter,f.dye.read);prefilter.texture("uTexture",f.dye.read.texture,0);
            float knee=p.bloomThreshold*p.bloomSoftKnee+.0001f;
            prefilter.three("curve",p.bloomThreshold-knee,knee*2,.25f/knee);prefilter.one("threshold",p.bloomThreshold);draw(f.bloom);
            Target* last=&f.bloom;
            for(auto& level:f.bloomLevels){begin(bloomBlur,*last);bloomBlur.texture("uTexture",last->texture,0);draw(level);last=&level;}
            for(int i=static_cast<int>(f.bloomLevels.size())-2;i>=0;--i){
                begin(bloomBlur,*last);bloomBlur.texture("uTexture",last->texture,0);glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE);draw(f.bloomLevels[i]);last=&f.bloomLevels[i];
            }
            begin(bloomFinal,*last);bloomFinal.texture("uTexture",last->texture,0);bloomFinal.one("intensity",p.bloomIntensity);draw(f.bloom);
        }
        if(p.sunrays) {
            begin(mask,f.dye.read);mask.texture("uTexture",f.dye.read.texture,0);draw(f.dye.write);
            begin(rays,f.dye.write);rays.texture("uTexture",f.dye.write.texture,0);rays.one("weight",p.sunraysWeight);draw(f.sunrays);
            begin(blur,f.sunrays);blur.two("texelSize",1.0f/f.sunrays.width,0);blur.texture("uTexture",f.sunrays.texture,0);draw(f.sunTemp);
            blur.two("texelSize",0,1.0f/f.sunrays.height);blur.texture("uTexture",f.sunTemp.texture,0);draw(f.sunrays);
        }
        auto& display=*displays[(p.shading?1:0)|(p.bloom?2:0)|(p.sunrays?4:0)];
        begin(display,f.dye.read);display.two("texelSize",1.0f/width,1.0f/height);
        display.texture("uTexture",f.dye.read.texture,0);display.texture("uBloom",f.bloom.texture,1);
        display.texture("uDithering",noise.texture,2);display.texture("uSunrays",f.sunrays.texture,3);
        display.two("ditherScale",width/8.0f,height/8.0f);display.one("uInvert",p.ink?1:0);
        glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,width,height);glDrawArrays(GL_TRIANGLES,0,3);
        glActiveTexture(GL_TEXTURE0);glBindVertexArray(0);glUseProgram(0);
        if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Fluid rendering GL error");
    }
};
FluidRenderer::FluidRenderer(int w,int h,const Config::Fluid& c)
    :FluidRenderer(w,h,c,fluid_gl::probeCapabilities()){}
FluidRenderer::FluidRenderer(int w,int h,const Config::Fluid& c,fluid_gl::Capabilities caps)
{
    fluid_gl::StateGuard restore;
    fluid_gl::resetState();
    impl_=std::make_unique<Impl>(w,h,c,caps);
}
FluidRenderer::~FluidRenderer()=default;
void FluidRenderer::resize(int w,int h){impl_->resize(w,h);}
void FluidRenderer::render(float dt,const FluidPreset& p,const std::vector<FluidSplat>& s,float persistence){impl_->render(dt,p,s,persistence);}
}
