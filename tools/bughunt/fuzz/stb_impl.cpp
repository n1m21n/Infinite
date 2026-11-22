// stb_image and stb_image_write implementations for fuzz targets that link
// MediaDecodePortable.cpp or TileStore.cpp without the rest of the app.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
