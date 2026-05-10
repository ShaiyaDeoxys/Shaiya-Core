// stb_image implementation — compiled once, linked into the DLL.
// Only PNG decoding is needed (emojis, roulette bg, titles).
// Disable unused decoders to reduce binary size.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include <external/stb/stb_image.h>
