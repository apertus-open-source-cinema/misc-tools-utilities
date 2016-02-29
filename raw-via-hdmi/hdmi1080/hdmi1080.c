/*
 * HDMI 1080p converter for Axiom BETA footage.
 * 
 * Copyright (C) 2013 a1ex
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include "stdint.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "ctype.h"
#include "unistd.h"
#include "sys/stat.h"
#include "math.h"
//#include "cmdoptions.h"
#include "patternnoise.h"
//#include "wirth.h"

/* image data */
uint16_t* rgb;
int width;
int height;

/* dark frame */
uint16_t* dark;

#define FAIL(fmt,...) { fprintf(stderr, "Error: "); fprintf(stderr, fmt, ## __VA_ARGS__); fprintf(stderr, "\n"); exit(1); }
#define CHECK(ok, fmt,...) { if (!(ok)) FAIL(fmt, ## __VA_ARGS__); }

#define MIN(a,b) \
   ({ __typeof__ ((a)+(b)) _a = (a); \
      __typeof__ ((a)+(b)) _b = (b); \
     _a < _b ? _a : _b; })

#define MAX(a,b) \
   ({ __typeof__ ((a)+(b)) _a = (a); \
      __typeof__ ((a)+(b)) _b = (b); \
     _a > _b ? _a : _b; })

#define COERCE(x,lo,hi) MAX(MIN((x),(hi)),(lo))

#define COUNT(x)        ((int)(sizeof(x)/sizeof((x)[0])))

static void change_ext(char* old, char* new, char* newext, int maxsize)
{
    snprintf(new, maxsize - strlen(newext), "%s", old);
    char* ext = strrchr(new, '.');
    if (!ext) ext = new + strlen(new);
    strcpy(ext, newext);
}

static int endswith(char* str, char* suffix)
{
    int l1 = strlen(str);
    int l2 = strlen(suffix);
    if (l1 < l2)
    {
        return 0;
    }
    if (strcmp(str + l1 - l2, suffix) == 0)
    {
        return 1;
    }
    return 0;
}

static void reverse_bytes_order(uint8_t* buf, int count)
{
    uint16_t* buf16 = (uint16_t*) buf;
    int i;
    for (i = 0; i < count/2; i++)
    {
        uint16_t x = buf16[i];
        buf[2*i+1] = x;
        buf[2*i] = x >> 8;
    }
}

/* 16-bit PPM, as converted by FFMPEG */
/* rgb is allocated by us */
static void read_ppm(char* filename, uint16_t** prgb)
{
    FILE* fp = fopen(filename, "rb");
    CHECK(fp, "could not open %s", filename);

    /* PGM read code from dcraw, adapted for PPM */
    int dim[3]={0,0,0}, comment=0, number=0, error=0, nd=0, c;

      if (fgetc(fp) != 'P' || fgetc(fp) != '6') error = 1;
      while (!error && nd < 3 && (c = fgetc(fp)) != EOF) {
        if (c == '#')  comment = 1;
        if (c == '\n') comment = 0;
        if (comment) continue;
        if (isdigit(c)) number = 1;
        if (number) {
          if (isdigit(c)) dim[nd] = dim[nd]*10 + c -'0';
          else if (isspace(c)) {
        number = 0;  nd++;
          } else error = 1;
        }
      }
    
    CHECK(!(error || nd < 3), "not a valid PGM file\n");

    width = dim[0];
    height = dim[1];

    CHECK(prgb && !*prgb, "prgb");
    uint16_t* rgb = malloc(width * height * 2 * 3);
    *prgb = rgb;
    
    int size = fread(rgb, 1, width * height * 2 * 3, fp);
    CHECK(size == width * height * 2 * 3, "fread");
    fclose(fp);
    
    /* PPM is big endian, need to reverse it */
    reverse_bytes_order((void*)rgb, width * height * 2 * 3);
}

/* Output 16-bit PPM file */
static void write_ppm(char* filename, uint16_t * rgb)
{
    printf("Writing %s...\n", filename);
    FILE* f = fopen(filename, "wb");
    fprintf(f, "P6\n%d %d\n65535\n", width, height);
    /* PPM is big endian, need to reverse it */
    reverse_bytes_order((void*)rgb, width * height * 2 * 3);
    fwrite(rgb, 1, width * height * 2 * 3, f);
    reverse_bytes_order((void*)rgb, width * height * 2 * 3);
    fclose(f);
}

static void convert_to_linear_and_subtract_darkframe(uint16_t * rgb, uint16_t * dark, int offset)
{
    for (int i = 0; i < width*height*3; i++)
    {
        /* test footage was recorded with gamma 0.5 */
        /* dark frame median is about 10000 */
        /* clipping point is about 50000 */
        double gain = (65535 - offset) / 40000.0;
        double rgb2 = rgb[i] / 65535.0; rgb2 *= rgb2;
        double dark2 = dark[i] / 65535.0; dark2 *= dark2;
        rgb[i] = COERCE((rgb2 - dark2) * 65535 * gain + offset, 0, 65535);
    }
}

int main(int argc, char** argv)
{
    if (argc == 1)
    {
        printf("HDMI 1080p converter for Axiom BETA\n");
        printf("\n");
        printf("Usage:\n");
        printf("  ffmpeg -i input.mov frames%%05d.ppm && %s *.ppm\n", argv[0]);
        printf("  %s input.mov output.mov [todo]\n", argv[0]);
        printf("\n");
        return 0;
    }

    printf("\n");
    
    printf("Reading darkframe-hdmi.ppm...\n");
    read_ppm("darkframe-hdmi.ppm", &dark);

    /* all other arguments are input or output files */
    for (int k = 1; k < argc; k++)
    {
        char* out_filename;

        printf("\n%s\n", argv[k]);
        
        if (endswith(argv[k], ".ppm"))
        {
            /* replace input file extension with .DNG */
            static char fo[256];
            change_ext(argv[k], fo, "-out.ppm", sizeof(fo));
            out_filename = fo;
            
            read_ppm(argv[k], &rgb);
        }
        
        printf("Linear and darkframe...\n");
        convert_to_linear_and_subtract_darkframe(rgb, dark, 1024);

        fix_pattern_noise(rgb, width, height, 0);

        printf("Output file : %s\n", out_filename);
        write_ppm(out_filename, rgb);

        free(rgb); rgb = 0;
    }
    
    printf("Done.\n\n");
    
    return 0;
}
