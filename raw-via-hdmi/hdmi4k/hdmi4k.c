/*
 * HDMI 4K converter for Axiom BETA footage.
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
#include "cmdoptions.h"
#include "patternnoise.h"
#include "ufraw_routines.h"

/* image data */
uint16_t* rgbA;     /* HDMI frame A (width x height x 3, PPM order) */
uint16_t* rgbB;     /* HDMI frame B */
uint16_t* raw;      /* Bayer raw image (double resolution) */
int width;
int height;

struct cmd_group options[] = {
    OPTION_GROUP_EOL
};

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

#define ABS(a) \
   ({ __typeof__ (a) _a = (a); \
     _a > 0 ? _a : -_a; })

#define COERCE(x,lo,hi) MAX(MIN((x),(hi)),(lo))

#define COUNT(x)        ((int)(sizeof(x)/sizeof((x)[0])))

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

/* Output 16-bit PGM file */
/* caveat: it reverses bytes order in the buffer */
static void write_pgm(char* filename, uint16_t * raw, int w, int h)
{
    printf("Writing %s...\n", filename);
    FILE* f = fopen(filename, "wb");
    fprintf(f, "P5\n%d %d\n65535\n", w, h);

    /* PPM is big endian, need to reverse it */
    reverse_bytes_order((void*)raw, w * h * 2);
    fwrite(raw, 1, w * h * 2, f);
    fclose(f);
}

static int file_exists(char * filename)
{
    struct stat buffer;   
    return (stat (filename, &buffer) == 0);
}

static int file_exists_warn(char * filename)
{
    int ans = file_exists(filename);
    if (!ans) printf("Not found   : %s\n", filename);
    return ans;
}

int main(int argc, char** argv)
{
    if (argc == 1)
    {
        printf("HDMI RAW converter for Axiom BETA\n");
        printf("\n");
        printf("Usage:\n");
        printf("  ffmpeg -i input.mov -vf \"framestep=2\" frame%%05dA.ppm\n");
        printf("  ffmpeg -ss 00.016 -i input.mov -vf \"framestep=2\" frame%%05dB.ppm\n");
        printf("  %s frame*A.ppm\n", argv[0]);
        printf("  raw2dng frame*.pgm [options]\n");
        printf("\n");
        printf("Calibration files:\n");
        printf("  hdmi-darkframe-A.ppm, hdmi-darkframe-A.ppm:\n");
        printf("  averaged dark frames from the HDMI recorder (even/odd frames)\n");
        printf("\n");
        show_commandline_help(argv[0]);
        return 0;
    }

    /* parse all command-line options */
    for (int k = 1; k < argc; k++)
        if (argv[k][0] == '-')
            parse_commandline_option(argv[k]);
    show_active_options();

    printf("\n");
    
    /* all other arguments are input or output files */
    for (int k = 1; k < argc; k++)
    {
        if (argv[k][0] == '-')
            continue;

        char* out_filename;

        printf("\n%s\n", argv[k]);
        
        if (endswith(argv[k], "A.ppm"))
        {
            /* replace input file extension (including the A character) with .pgm */
            static char fo[256];
            int len = strlen(argv[k]) - 5;
            snprintf(fo, sizeof(fo), "%s", argv[k]);
            snprintf(fo+len, sizeof(fo)-len, ".pgm");
            out_filename = fo;
            
            read_ppm(argv[k], &rgbA);
            argv[k][len] = 'B';
            read_ppm(argv[k], &rgbB);
            raw = malloc(width * height * 4 * sizeof(raw[0]));
        }
        else if (endswith(argv[k], "B.ppm"))
        {
            printf("Ignored (please specify only A frames).\n");
            continue;
        }
        else if (endswith(argv[k], ".ppm"))
        {
            printf("Input files should end in A.ppm.\n");
            continue;
        }
        else
        {
            printf("Unknown file type.\n");
            continue;
        }
        
        /* copy input data to output, just to check the basics */
        for (int y = 0; y < height; y++)
        {
            for (int x = 0; x < width; x++)
            {
                /* green1 from B, green2 from A, red/blue from B */
                raw[2*x   + (2*y  ) * (2*width)] = rgbB[3*x+1 + y*width*3] / 16;
                raw[2*x+1 + (2*y+1) * (2*width)] = rgbA[3*x+1 + y*width*3] / 16;
                raw[2*x   + (2*y+1) * (2*width)] = rgbB[3*x   + y*width*3] / 16;
                raw[2*x+1 + (2*y  ) * (2*width)] = rgbB[3*x+2 + y*width*3] / 16;
            }
        }
        
        printf("Output file : %s\n", out_filename);
        write_pgm(out_filename, raw, 2*width, 2*height);

        free(rgbA); rgbA = 0;
        free(rgbB); rgbB = 0;
        free(raw);  raw = 0;
    }
    
    printf("Done.\n\n");
    
    return 0;
}
