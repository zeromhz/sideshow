#include "isf_presenter.h"
#include <algorithm>
namespace sideshow {
IsfPresenter::IsfPresenter():program_(R"(
out vec2 uv;
void main(){vec2 p=vec2(float((gl_VertexID&1)<<2)-1.0,float((gl_VertexID&2)<<1)-1.0);uv=(p+1.0)*0.5;gl_Position=vec4(p,0,1);}
)",R"(
in vec2 uv;uniform sampler2D image;out vec4 color;
void main(){vec4 c=texture(image,uv);color=vec4(c.rgb*clamp(c.a,0.0,1.0),1.0);}
)"){}
void IsfPresenter::present(GLuint texture,int width,int height,int dw,int dh){
 if(dw<=0||dh<=0)return;fluid_gl::resetState();glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,dw,dh);const GLfloat black[]={0,0,0,1};glClearBufferfv(GL_COLOR,0,black);
 double aspect=double(width)/height;int w=dw,h=dh;if(double(dw)/dh>aspect)w=std::max(1,int(h*aspect));else h=std::max(1,int(w/aspect));glViewport((dw-w)/2,(dh-h)/2,w,h);
 program_.use();vao_.bind();program_.texture("image",texture,0);glDrawArrays(GL_TRIANGLES,0,3);
}
}
