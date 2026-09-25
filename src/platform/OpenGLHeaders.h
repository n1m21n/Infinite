#pragma once

// Infinite-Turbo is Windows-only: GLEW loads the OpenGL entry points.
#ifndef GLEW_STATIC
#define GLEW_STATIC
#endif
#include <GL/glew.h>

// Turbo: uniform-location cache.
// The node code asks the driver for uniform locations by name on every cook
// (hundreds of glGetUniformLocation calls per frame). Each one is a string
// lookup inside the driver and, with NVIDIA "Threaded Optimization", can force
// the app thread to wait for the driver thread. Locations never change after a
// program is linked, so every call site is transparently redirected to a
// per-program cache here. glLinkProgram / glDeleteProgram are redirected too so
// a re-linked or recycled program id never serves stale locations.
// GLUtil.cpp defines INFINITE_GL_NO_REDIRECT to reach the real functions.
namespace GLUtil
{
   GLint CachedUniformLocation(GLuint program, const GLchar* name);
   void LinkProgramInvalidate(GLuint program);
   void DeleteProgramInvalidate(GLuint program);
}
#ifndef INFINITE_GL_NO_REDIRECT
#undef glGetUniformLocation
#define glGetUniformLocation(program, name) ::GLUtil::CachedUniformLocation((GLuint)(program), (name))
#undef glLinkProgram
#define glLinkProgram(program) ::GLUtil::LinkProgramInvalidate((GLuint)(program))
#undef glDeleteProgram
#define glDeleteProgram(program) ::GLUtil::DeleteProgramInvalidate((GLuint)(program))
#endif
