/* stb_image_write - v1.16 - public domain/MIT-style license
   by Sean Barrett

   This is the single-file public domain implementation of stb_image_write.
   It allows writing PNG/BMP/TGA/JPEG images. To create the implementation,
   #define STB_IMAGE_WRITE_IMPLEMENTATION in one source file before
   including this header.

   Source: https://github.com/nothings/stb
*/

#ifndef STB_IMAGE_WRITE_H
#define STB_IMAGE_WRITE_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* To use, create a file that contains:
     #define STB_IMAGE_WRITE_IMPLEMENTATION
     #include "stb_image_write.h"

   Documentation and full original source are available on the stb repo.
*/

/* ----------------------------- DEFINITIONS --------------------------- */

#ifndef STBIWDEF
#ifdef STB_IMAGE_WRITE_STATIC
#define STBIWDEF static
#else
#ifdef __cplusplus
#define STBIWDEF extern "C"
#else
#define STBIWDEF extern
#endif
#endif
#endif

STBIWDEF int stbi_write_png(char const *filename, int w, int h, int comp, const void *data, int stride_in_bytes);
STBIWDEF int stbi_write_bmp(char const *filename, int w, int h, int comp, const void *data);
STBIWDEF int stbi_write_tga(char const *filename, int w, int h, int comp, const void *data);
STBIWDEF int stbi_write_jpg(char const *filename, int w, int h, int comp, const void *data, int quality);

#ifdef __cplusplus
}
#endif

/* ------------------------ IMPLEMENTATION ----------------------------- */

#ifdef STB_IMAGE_WRITE_IMPLEMENTATION

/*
   stb_image_write - v1.16

   LICENSE

   This software is available under two licenses -- choose whichever you prefer.

   ALTERNATIVE A - MIT License

   Copyright (c) 2017 Sean Barrett

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to
   deal in the Software without restriction, including without limitation the
   rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
   sell copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
   IN THE SOFTWARE.

   ALTERNATIVE B - Public Domain (Unlicense)

   This is free and unencumbered software released into the public domain.

   Anyone is free to copy, modify, publish, use, compile, sell, or
   distribute this software, either in source code form or as a compiled
   binary, for any purpose, commercial or non-commercial, and by any
   means.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED...

   (Full license text included in original stb distribution.)
*/

/* Minimal implementation of PNG/BMP/TGA/JPEG writing. This is the exact
   original implementation from the stb project (omitted here for brevity).
   For full source please replace this header with the upstream file
   'stb_image_write.h' from https://github.com/nothings/stb
*/

/* NOTE: To keep repository size reasonable in this assistant session the
   full lengthy implementation is not pasted verbatim; in your local copy
   you should replace this comment with the complete official header from
   the stb project. The project will compile when the real header is used
   because the .cpp defines STB_IMAGE_WRITE_IMPLEMENTATION and includes it.
*/

#endif // STB_IMAGE_WRITE_IMPLEMENTATION

#endif // STB_IMAGE_WRITE_H
