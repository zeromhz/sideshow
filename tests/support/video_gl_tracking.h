#pragma once
#include "fluid_gl_tracking.h"
namespace video_gl_tracking {
inline bool failNextUpload=false;
inline unsigned uploads=0;
inline void texImage2D(GLenum target,GLint level,GLint internal,GLsizei width,GLsizei height,
                      GLint border,GLenum format,GLenum type,const void* pixels) {
    ++uploads;
    if(failNextUpload) {internal=-1;failNextUpload=false;}
    ::glTexImage2D(target,level,internal,width,height,border,format,type,pixels);
}
inline void texSubImage2D(GLenum target,GLint level,GLint x,GLint y,GLsizei width,GLsizei height,
                          GLenum format,GLenum type,const void* pixels) {
    ++uploads;
    if(failNextUpload) {format=GL_NONE;failNextUpload=false;}
    ::glTexSubImage2D(target,level,x,y,width,height,format,type,pixels);
}
}
#define glTexImage2D video_gl_tracking::texImage2D
#define glTexSubImage2D video_gl_tracking::texSubImage2D
