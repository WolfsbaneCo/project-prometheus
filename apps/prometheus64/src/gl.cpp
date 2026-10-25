#include "gl.hpp"

#include <SDL3/SDL_video.h>

namespace gl {

#define P64_DEFINE(name) decltype(&::name) name = nullptr;
P64_GL_FUNCTIONS(P64_DEFINE)
#undef P64_DEFINE

const char* load() {
#define P64_LOAD(fn)                                                     \
  fn = reinterpret_cast<decltype(fn)>(SDL_GL_GetProcAddress(#fn));       \
  if (!fn) return #fn;
  P64_GL_FUNCTIONS(P64_LOAD)
#undef P64_LOAD
  return nullptr;
}

}  // namespace gl
