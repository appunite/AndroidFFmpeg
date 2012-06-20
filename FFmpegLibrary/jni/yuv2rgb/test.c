/* YUV-> RGB conversion code.
 *
 * Copyright (C) 2011 Robin Watts (robin@wss.co.uk) for Pinknoise
 * Productions Ltd.
 *
 * Licensed under the BSD license. See 'COPYING' for details of
 * (non-)warranty.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include "yuv2rgb.h"

#define WIDTH  256
#define HEIGHT 192

static uint8_t rgba[WIDTH*HEIGHT*4];
static uint8_t y[WIDTH*HEIGHT];
static uint8_t u[WIDTH*HEIGHT>>2];
static uint8_t v[WIDTH*HEIGHT>>2];

int main(int argc, char *argv[]) {
    FILE *file;
    int   i;

    file = fopen("out.yuv", "rb");
    if (file == NULL) {
        fprintf(stderr, "Failed to read out.yuv\n");
        return EXIT_FAILURE;
    }

    fread(y, WIDTH*HEIGHT,    1, file);
    fread(u, WIDTH*HEIGHT>>2, 1, file);
    fread(v, WIDTH*HEIGHT>>2, 1, file);
    fclose(file);

    yuv420_2_rgb8888(rgba,
                     y,
                     u,
                     v,
                     WIDTH,
                     HEIGHT,
                     WIDTH,
                     WIDTH>>1,
                     WIDTH<<2,
                     yuv2bgr565_table,
                     0);

    file = fopen("out.pnm", "wb");
    if (file == NULL) {
        fprintf(stderr, "Failed to write out.pnm\n");
        return EXIT_FAILURE;
    }

    fprintf(file, "P6 256 192 256\n");
    for(i=0; i < WIDTH*HEIGHT; i++) {
        fputc(rgba[i*4  ], file);
        fputc(rgba[i*4+1], file);
        fputc(rgba[i*4+2], file);
    }
    fclose(file);
}

int WinMain(void) {
    return main(0, NULL);
}

int wmain(void) {
    return main(0, NULL);
}
