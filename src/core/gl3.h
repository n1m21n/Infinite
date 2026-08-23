#pragma once

// Cross-platform OpenGL 3.x core-profile header.
//
// macOS ships <OpenGL/gl3.h> with the 3.2 core profile as the system limit;
// everything else uses the glad loader (external/glad, generated for GL 3.3
// core). The glad header only declares function pointers - the definitions
// live in external/glad/src/gl.c, compiled into the target, and the pointers
// must be filled in once after the GL context is current:
//
//     gladLoadGL((GLADloadfunc)glfwGetProcAddress);
//
// (main.cpp does this right after glfwMakeContextCurrent on non-Apple
// platforms; on macOS the system headers need no loading step.)

#if defined(__APPLE__)
   #include <OpenGL/gl3.h>
#else
   // glad2's generated naming: include/glad/gl.h + src/gl.c (not glad.h).
   #include "glad/gl.h"
#endif
