#pragma once
#include "fluid_gl_tracking.h"
namespace video_gl_tracking {
inline bool failNextUpload=false;
inline void texImage2D(GLenum target,GLint level,GLint internal,GLsizei width,GLsizei height,
                      GLint border,GLenum format,GLenum type,const void* pixels) {
    if(failNextUpload) {internal=-1;failNextUpload=false;}
    ::glTexImage2D(target,level,internal,width,height,border,format,type,pixels);
}
}
#define glTexImage2D video_gl_tracking::texImage2D
