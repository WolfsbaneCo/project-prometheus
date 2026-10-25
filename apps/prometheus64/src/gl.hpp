// Minimal OpenGL 3.3 core loader. Every function is fetched through SDL at runtime, so the
// app links against no OpenGL library and shares one context with the emulator core.
#pragma once

#define GL_GLEXT_PROTOTYPES 1  // declarations only, used for decltype; nothing links to them
#include <SDL3/SDL_opengl.h>

#define P64_GL_FUNCTIONS(X)                                                                     \
  X(glActiveTexture) X(glAttachShader) X(glBindBuffer) X(glBindFramebuffer)                     \
  X(glBindRenderbuffer) X(glBindTexture) X(glBindVertexArray) X(glBlendFunc)                    \
  X(glBufferData) X(glCheckFramebufferStatus) X(glClear) X(glClearColor) X(glCompileShader)     \
  X(glCreateProgram) X(glCreateShader) X(glDeleteBuffers) X(glDeleteFramebuffers)               \
  X(glDeleteProgram) X(glDeleteRenderbuffers) X(glDeleteShader) X(glDeleteTextures)             \
  X(glDeleteVertexArrays) X(glDisable) X(glDrawArrays) X(glEnable) X(glEnableVertexAttribArray) \
  X(glFramebufferRenderbuffer) X(glFramebufferTexture2D) X(glGenBuffers) X(glGenFramebuffers)   \
  X(glGenRenderbuffers) X(glGenTextures) X(glGenVertexArrays) X(glGenerateMipmap)               \
  X(glGetError) X(glGetProgramInfoLog) X(glGetProgramiv) X(glGetShaderInfoLog) X(glGetShaderiv) \
  X(glGetString) X(glGetUniformLocation) X(glLinkProgram) X(glPixelStorei) X(glReadPixels)      \
  X(glRenderbufferStorage) X(glScissor) X(glShaderSource) X(glTexImage2D) X(glTexParameteri)    \
  X(glTexSubImage2D) X(glUniform1f) X(glUniform1i) X(glUniform2f) X(glUniform4f)                \
  X(glUseProgram) X(glVertexAttribPointer) X(glViewport) X(glColorMask) X(glDepthMask)          \
  X(glBindSampler) X(glPolygonMode) X(glBlendEquation)

namespace gl {

#define P64_DECLARE(name) extern decltype(&::name) name;
P64_GL_FUNCTIONS(P64_DECLARE)
#undef P64_DECLARE

// Loads every function above from the current context. Returns the first missing name, or
// nullptr when all were found.
const char* load();

}  // namespace gl
