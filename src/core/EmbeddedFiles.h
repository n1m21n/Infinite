#pragma once

// Turbo 0.70 (A5): text files built into the exe by cmake/EmbedFiles.cmake -
// the GLSL under shaders/ and the third-party GLSL headers they use (AMD
// FidelityFX FSR 1). Looked up by file name ("render3d_taa.comp.glsl");
// returns the null-terminated contents, or nullptr when no such file was
// embedded.
const char* EmbeddedFile(const char* name);
