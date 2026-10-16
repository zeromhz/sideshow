#pragma once
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdexcept>
inline void requireIsf(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct IsfTestContext {
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    IsfTestContext(int width=320, int height=180) {
        SDL_SetMainReady();
        requireIsf(SDL_Init(SDL_INIT_VIDEO)==0, SDL_GetError());
#ifdef SIDESHOW_GLES
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
#else
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
#endif
        window=SDL_CreateWindow("ISF integration",0,0,width,height,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
        context=window ? SDL_GL_CreateContext(window) : nullptr;
        if (!context) { if(window) SDL_DestroyWindow(window); SDL_Quit(); throw std::runtime_error("ISF GL context unavailable"); }
    }
    ~IsfTestContext() { SDL_GL_DeleteContext(context); SDL_DestroyWindow(window); SDL_Quit(); }
};
