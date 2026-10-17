#pragma once
#include "gl.h"
#include "isf_context_internal.h"

namespace isf_gl_failure {
inline bool failNextCheck=false;
inline GLenum getError() {
    const auto error=::glGetError();
    if(failNextCheck) {
        failNextCheck=false;
        return GL_OUT_OF_MEMORY;
    }
    return error;
}
}
#define glGetError isf_gl_failure::getError
