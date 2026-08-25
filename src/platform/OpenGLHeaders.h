#pragma once

// Keep the renderer source identical on macOS and Windows. Apple exposes the
// OpenGL 3.2 core entry points directly; Windows needs an extension loader.
#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#elif defined(_WIN32)
#ifndef GLEW_STATIC
#define GLEW_STATIC
#endif
#include <GL/glew.h>
#else
#include <GL/gl.h>
#endif
