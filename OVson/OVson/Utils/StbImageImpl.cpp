// stb_image is header-only: exactly one translation unit in the whole binary
// must define STB_IMAGE_IMPLEMENTATION so the function bodies get emitted,
// while every other file that includes the header sees declarations only.
//
// That translation unit used to be Render/TextureLoader.cpp, which existed
// solely to load the bed-defense block textures and was deleted along with
// that feature. ClickGUI/Input.cpp still decodes downloaded PNGs through
// stbi_load_from_memory, so removing TextureLoader.cpp took the definitions
// out from under it and the link failed on stbi_load_from_memory and
// stbi_image_free.
//
// This file exists only to own that definition, so it is not tied to any
// feature that might be removed later. Deliberately kept out of the /W4 list
// in CMakeLists.txt: stb_image is vendored third-party code and is not clean
// at that warning level.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
