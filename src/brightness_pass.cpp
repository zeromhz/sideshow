#include "brightness_pass.h"

#include <stdexcept>
#include <string>

namespace sideshow {
namespace {

#ifdef SIDESHOW_GLES
const char* kHeader = "#version 300 es\nprecision mediump float;\n";
#else
const char* kHeader = "#version 330 core\n";
#endif

// Fullscreen triangle from gl_VertexID: (-1,-1), (3,-1), (-1,3). No vertex buffers needed.
const char* kVertexBody = R"(
void main() {
    vec2 pos = vec2(float((gl_VertexID & 1) << 2) - 1.0, float((gl_VertexID & 2) << 1) - 1.0);
    gl_Position = vec4(pos, 0.0, 1.0);
}
)";

const char* kFragmentBody = R"(
uniform float u_brightness;
out vec4 fragColor;
void main() {
    fragColor = vec4(0.0, 0.0, 0.0, u_brightness);
}
)";

GLuint compileShader(GLenum type, const char* body) {
    GLuint shader = glCreateShader(type);
    const char* sources[] = {kHeader, body};
    glShaderSource(shader, 2, sources, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        glDeleteShader(shader);
        throw std::runtime_error(std::string("brightness shader compile failed: ") + log);
    }
    return shader;
}

}  // namespace

BrightnessPass::BrightnessPass() {
    GLuint vertex = compileShader(GL_VERTEX_SHADER, kVertexBody);
    GLuint fragment = compileShader(GL_FRAGMENT_SHADER, kFragmentBody);
    program_ = glCreateProgram();
    glAttachShader(program_, vertex);
    glAttachShader(program_, fragment);
    glLinkProgram(program_);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
        glDeleteProgram(program_);
        throw std::runtime_error(std::string("brightness program link failed: ") + log);
    }
    brightnessLocation_ = glGetUniformLocation(program_, "u_brightness");
    glGenVertexArrays(1, &vao_);  // core profile / GLES3 need a bound VAO to draw
}

BrightnessPass::~BrightnessPass() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (program_) glDeleteProgram(program_);
}

void BrightnessPass::apply(float brightness, int width, int height) {
    if (brightness >= 0.999f) return;  // full brightness: nothing to do

    // Save the state we touch so projectM finds it as it left it.
    GLint prevProgram = 0, prevVao = 0;
    GLint srcRgb = 0, dstRgb = 0, srcAlpha = 0, dstAlpha = 0;
    const GLboolean blendWasOn = glIsEnabled(GL_BLEND);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_BLEND_SRC_RGB, &srcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &dstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &dstAlpha);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ZERO, GL_SRC_ALPHA);  // result = framebuffer * brightness
    glUseProgram(program_);
    glUniform1f(brightnessLocation_, brightness);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindVertexArray(static_cast<GLuint>(prevVao));
    glUseProgram(static_cast<GLuint>(prevProgram));
    glBlendFuncSeparate(srcRgb, dstRgb, srcAlpha, dstAlpha);
    if (!blendWasOn) glDisable(GL_BLEND);
}

}  // namespace sideshow
