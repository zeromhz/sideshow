#include "support/isf_gl_context.h"
#include "isf_context.h"
#include "isf_context_internal.h"
#include "VVISF.hpp"
#include <array>
#include <iostream>
std::array<unsigned char,4> pixel(const VVGL::GLBufferRef& texture) {
    GLuint fbo=0; glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,texture->desc.target,texture->name,0);
    requireIsf(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"incomplete output");
    std::array<unsigned char,4> result{};
    glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,result.data());
    glBindFramebuffer(GL_FRAMEBUFFER,0); glDeleteFramebuffers(1,&fbo);
    return result;
}
void renderChecks(sideshow::IsfContext& runtime) {
    auto& impl=runtime.impl();
    VVISF::ISFScene scene(impl.context);
    scene.setPrivatePool(impl.pool); scene.setThrowExceptions(true);
    auto doc=std::make_shared<VVISF::ISFDoc>(R"(/*{"ISFVSN":"2.0"}*/
void main(){gl_FragColor=vec4(1.0,0.0,0.0,1.0);})",VVISF::ISFVertPassthru_GL2,"",&scene,true);
    scene.useDoc(doc);
    auto output=VVGL::CreateRGBATex(VVGL::Size(32,32),true,impl.pool);
    scene.renderToBuffer(output,VVGL::Size(32,32),0.0);
    auto red=pixel(output);
    requireIsf(red[0]==255&&red[1]==0&&red[2]==0&&red[3]==255,"constant-color output incorrect");
    doc=std::make_shared<VVISF::ISFDoc>(R"(/*{"ISFVSN":"2.0","PASSES":[{"TARGET":"history","PERSISTENT":true},{}]}*/
void main(){if(PASSINDEX==0)gl_FragColor=vec4(IMG_THIS_PIXEL(history).r+0.1,0.0,0.0,1.0);else gl_FragColor=IMG_THIS_PIXEL(history);})",VVISF::ISFVertPassthru_GL2,"",&scene,true);
    scene.useDoc(doc);
    scene.renderToBuffer(output,VVGL::Size(32,32),0.0); auto first=pixel(output);
    scene.renderToBuffer(output,VVGL::Size(32,32),0.1); auto second=pixel(output);
    std::cout << "feedback red: " << int(first[0]) << " -> " << int(second[0]) << '\n';
    requireIsf(first[0]>=24&&first[0]<=27&&second[0]>=49&&second[0]<=54,"persistent multipass history incorrect");
}
int main() {
    try {
        IsfTestContext window;
        auto before=SDL_GL_GetCurrentContext();
        { sideshow::IsfContext runtime; requireIsf(SDL_GL_GetCurrentContext()==before,"context changed"); renderChecks(runtime); }
        requireIsf(SDL_GL_GetCurrentContext()==before,"context destroyed");
        glClear(GL_COLOR_BUFFER_BIT);
        requireIsf(glGetError()==GL_NO_ERROR,"retained context unusable");
        std::cout << "ISF context checks passed\n";
    } catch(const VVISF::ISFErr& e) { std::cerr << e.general << ": " << e.specific << '\n'; return 1; } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
