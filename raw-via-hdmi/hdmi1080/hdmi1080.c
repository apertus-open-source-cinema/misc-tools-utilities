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
#include "cmdoptions.h"
#include "patternnoise.h"
//#include "wirth.h"

/* image data */
uint16_t* rgb;
int width;
int height;

/* dark frame */
uint16_t* dark;

/* options */
int fixpn = 0;
int fixpn_flags1;
int fixpn_flags2;
float exposure = 1;
int filter = 0;

struct cmd_group options[] = {
    {
        "Processing options", (struct cmd_option[]) {
            { &fixpn,          1,  "--fixpn",        "Fix row and column noise (SLOW, guesswork)" },
            { (void*)&exposure,1,  "--exposure=%f",  "Exposure compensation" },
            { &filter,         1,  "--filter=%d",    "Use a RGB filter (valid values: 1)" },
            OPTION_EOL,
        },
    },
    {
        "Debug options", (struct cmd_option[]) {
            { &fixpn_flags1,   FIXPN_DBG_DENOISED,  "--fixpn-dbg-denoised", "Pattern noise: show denoised image" },
            { &fixpn_flags1,   FIXPN_DBG_NOISE,     "--fixpn-dbg-noise",    "Pattern noise: show noise image (original - denoised)" },
            { &fixpn_flags1,   FIXPN_DBG_MASK,      "--fixpn-dbg-mask",     "Pattern noise: show masked areas (edges and highlights)" },
            { &fixpn_flags2,   FIXPN_DBG_COLNOISE,  "--fixpn-dbg-col",      "Pattern noise: debug columns (default: rows)" },
            OPTION_EOL,
        },
    },
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
        double gain = (65535 - offset) / 40000.0 * exposure;
        double rgb2 = rgb[i] / 65535.0; rgb2 *= rgb2;
        double dark2 = dark[i] / 65535.0; dark2 *= dark2;
        rgb[i] = COERCE((rgb2 - dark2) * 65535 * gain + offset, 0, 65535);
    }
}

const int filters[3][3][3][3] = {
    /* Red: */
    {
        /* from red: */
        {
            {   464,  900,  426 },
            {   620, 2889,  735 },
            {   388,  171,  540 },
        },
        /* from green: */
        {
            {  -491,  158,  386 },
            {  -678, 2979,  320 },
            {  -528,  -57, -577 },
        },
        /* from blue: */
        {
            {     3, -458, -115 },
            {  -182,  548, -556 },
            {    97,   53, -484 },
        },
    },
    /* Green: */
    {
        /* from red: */
        {
            {  -673,  364, -338 },
            {  -543, 1981, -149 },
            {  -299, -242,  -95 },
        },
        /* from green: */
        {
            {   726,  178,  879 },
            {   809, 2973,  822 },
            {   973,   94,  287 },
        },
        /* from blue: */
        {
            {  -222, -211, -360 },
            {   -24, 1219, -403 },
            {  -180,  608, -520 },
        },
    },
    /* Blue: */
    {
        /* from red: */
        {
            {  -399,  -56, -214 },
            {  -402,  904, -312 },
            {   -58, -300,  -71 },
        },
        /* from green: */
        {
            {   283, -120, -257 },
            {   478, 1530, -419 },
            {   556,  -73, -567 },
        },
        /* from blue: */
        {
            {   242,  452,  355 },
            {   711, 1779,  952 },
            {   565, 1104,  931 },
        },
    },
};

static void rgb_filter_1(uint16_t* rgb)
static void rgb_filter_1(uint16_t* rgb, const int filters[3][3][3][3])
{
    int size = width * height * 3 * sizeof(int32_t);
    int32_t * aux = malloc(size);
    memset(aux, 0, size);
    
    for (int y = 1; y < height-1; y++)
    {
        for (int x = 1; x < width-1; x++)
        {
            /* compute channel c */
            for (int c = 0; c < 3; c++)
            {
                /* from predictor channel p */
                for (int p = 0; p < 3; p++)
                {
                    aux[x*3+c + y*width*3] +=
                        filters[c][p][0][0] * rgb[(x-1)*3+c + (y-1)*width*3] + filters[c][p][0][1] * rgb[x*3+c + (y-1)*width*3] + filters[c][p][0][2] * rgb[(x+1)*3+c + (y-1)*width*3] +
                        filters[c][p][1][0] * rgb[(x-1)*3+c + (y+0)*width*3] + filters[c][p][1][1] * rgb[x*3+c + (y+0)*width*3] + filters[c][p][1][2] * rgb[(x+1)*3+c + (y+0)*width*3] +
                        filters[c][p][2][0] * rgb[(x-1)*3+c + (y+1)*width*3] + filters[c][p][2][1] * rgb[x*3+c + (y+1)*width*3] + filters[c][p][2][2] * rgb[(x+1)*3+c + (y+1)*width*3];
                }
            }
        }
    }

    for (int i = 0; i < width * height * 3; i++)
    {
        rgb[i] = COERCE(aux[i] / 8192, 0, 65535);
    }
    
    free(aux);
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
        show_commandline_help(argv[0]);
        return 0;
    }

    /* parse all command-line options */
    for (int k = 1; k < argc; k++)
        if (argv[k][0] == '-')
            parse_commandline_option(argv[k]);
    show_active_options();

    printf("\n");
    
    printf("Reading darkframe-hdmi.ppm...\n");
    read_ppm("darkframe-hdmi.ppm", &dark);

    /* all other arguments are input or output files */
    for (int k = 1; k < argc; k++)
    {
        if (argv[k][0] == '-')
            continue;

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

        if (fixpn)
        {
            int fixpn_flags = fixpn_flags1 | fixpn_flags2;
            fix_pattern_noise(rgb, width, height, fixpn_flags);
        }
        
        if (filter == 1)
        {
            printf("Filtering image...\n");
            rgb_filter_1(rgb, filters_1);
        }

        printf("Output file : %s\n", out_filename);
        write_ppm(out_filename, rgb);

        free(rgb); rgb = 0;
    }
    
    printf("Done.\n\n");
    
    return 0;
}
