#include "ui_image.h"
#include <limits.h>
#include <stdint.h>
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_SIMD
#define STB_IMAGE_IMPLEMENTATION
/* STBI_ONLY_PNG legitimately leaves other codec helpers unused. Limit the
 * warning exception to the third-party implementation, not our code. */
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "../vendor/stb_image.h"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/* The GPU framebuffer is 32-bit AARRGGBB. Alpha from source icons is
 * composited on royal-blue panel color, preventing undefined blend state. */
int ui_image_thumbnail(const unsigned char *png, size_t bytes,
                       uint32_t *argb, size_t capacity) {
    if (!png || !argb || bytes < 33 || bytes > UI_ICON_MAX_PNG ||
        bytes > INT_MAX || capacity < UI_ICON_PIXELS) return -1;
    if (png[0] != 0x89 || png[1] != 'P' || png[2] != 'N' ||
        png[3] != 'G' || png[4] != 0x0d || png[5] != 0x0a ||
        png[6] != 0x1a || png[7] != 0x0a) return -2;
    int width=0,height=0,channels=0;
    if (!stbi_info_from_memory(png,(int)bytes,&width,&height,&channels))
        return -3;
    if (width <= 0 || height <= 0 || width > 1024 || height > 1024 ||
        channels < 1 || channels > 4 ||
        (size_t)width * (size_t)height > (size_t)1024 * 1024)
        return -4;
    int decoded_width=0,decoded_height=0,decoded_channels=0;
    unsigned char *pixels=stbi_load_from_memory(
        png,(int)bytes,&decoded_width,&decoded_height,&decoded_channels,4
    );
    if (!pixels) return -5;
    if (width != decoded_width || height != decoded_height) {
        stbi_image_free(pixels);
        return -6;
    }
    for(unsigned y=0;y<UI_ICON_SIDE;++y) {
        unsigned sy=(unsigned)((uint64_t)y*(unsigned)height/UI_ICON_SIDE);
        for(unsigned x=0;x<UI_ICON_SIDE;++x) {
            unsigned sx=(unsigned)((uint64_t)x*(unsigned)width/UI_ICON_SIDE);
            size_t i=((size_t)sy*(unsigned)width+sx)*4;
            unsigned a=pixels[i+3];
            unsigned r=(pixels[i+0]*a+0x15u*(255u-a))/255u;
            unsigned g=(pixels[i+1]*a+0x28u*(255u-a))/255u;
            unsigned b=(pixels[i+2]*a+0x42u*(255u-a))/255u;
            argb[(size_t)y*UI_ICON_SIDE+x]=
                0xFF000000u|(r<<16)|(g<<8)|b;
        }
    }
    stbi_image_free(pixels);
    return 0;
}
