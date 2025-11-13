// Minimal PNG writer using libpng. This provides real PNG files when libpng
// is available on the system. It implements stbi_write_png signature used by
// the rest of the project.

#include "stb_image_write.h"
#include <png.h>
#include <stdio.h>
#include <stdlib.h>

int stbi_write_png(const char *filename, int w, int h, int comp, const void *data, int stride_in_bytes){
    FILE *fp = fopen(filename, "wb");
    if (!fp) return 0;

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) { fclose(fp); return 0; }
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) { png_destroy_write_struct(&png_ptr, (png_infopp)NULL); fclose(fp); return 0; }
    if (setjmp(png_jmpbuf(png_ptr))) { png_destroy_write_struct(&png_ptr, &info_ptr); fclose(fp); return 0; }

    png_init_io(png_ptr, fp);

    int color_type = (comp == 4) ? PNG_COLOR_TYPE_RGBA : PNG_COLOR_TYPE_RGB;
    png_set_IHDR(png_ptr, info_ptr, w, h, 8, color_type, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
    png_write_info(png_ptr, info_ptr);

    // set up row pointers
    png_bytep *row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * h);
    if (!row_pointers) { png_destroy_write_struct(&png_ptr, &info_ptr); fclose(fp); return 0; }
    for (int y = 0; y < h; ++y) {
        row_pointers[y] = (png_bytep)((const unsigned char*)data + y * stride_in_bytes);
    }

    png_write_image(png_ptr, row_pointers);
    png_write_end(png_ptr, NULL);

    free(row_pointers);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
    return 1;
}
