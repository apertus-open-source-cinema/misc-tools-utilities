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
int out_4k = 0;

struct cmd_group options[] = {
    {
        "Processing options", (struct cmd_option[]) {
            { &fixpn,          1,  "--fixpn",        "Fix row and column noise (SLOW, guesswork)" },
            { (void*)&exposure,1,  "--exposure=%f",  "Exposure compensation" },
            { &filter,         1,  "--filter=%d",    "Use a RGB filter (valid values: 1)" },
            { &out_4k,         1,  "--4k",           "Experimental 4K output" },
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

const int filters_1[3][3][3][3] = {
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

const int filters_4k[4][3][3][3][3] = {
    /* Sub-image #1 (0,0): */
    {
        /* Red: */
        {
            /* from red: */
            {
                {  1074, 1365,   61 },
                {   637, 2110,  164 },
                {   650,  416,  549 },
            },
            /* from green: */
            {
                {  -533,  854,  343 },
                {  -350, 3785, -709 },
                { -1093, -312, -383 },
            },
            /* from blue: */
            {
                {  -333, -566,  -59 },
                {   662, 1088, -104 },
                {  -179,-1167, -429 },
            },
        },
        /* Green: */
        {
            /* from red: */
            {
                {  -335,  791, -689 },
                {  -585, 1327, -573 },
                {   -42,   19,   29 },
            },
            /* from green: */
            {
                {   812,  745,  914 },
                {  1275, 3546,  -61 },
                {   412, -287,  429 },
            },
            /* from blue: */
            {
                {  -516, -249, -377 },
                {   795, 1803,  -91 },
                {  -409, -480, -558 },
            },
        },
        /* Blue: */
        {
            /* from red: */
            {
                {  -210,  188, -432 },
                {  -437,  501, -551 },
                {    73, -110,   31 },
            },
            /* from green: */
            {
                {   304,  143, -310 },
                {   815, 1858, -929 },
                {   250, -384, -309 },
            },
            /* from blue: */
            {
                {   205,  499,  435 },
                {  1315, 2196, 1119 },
                {   333,  349,  646 },
            },
        },
    },
    /* Sub-image #2 (0,1): */
    {
        /* Red: */
        {
            /* from red: */
            {
                {   493, 1391,  775 },
                {    35, 1880, 1088 },
                {   386,  308,  766 },
            },
            /* from green: */
            {
                {  -626,  676,  629 },
                {  -271, 2574,  182 },
                {  -416, -878, -352 },
            },
            /* from blue: */
            {
                {  -259, -649, -382 },
                {  -767, 2109,  655 },
                {   175, -683,-1294 },
            },
        },
        /* Green: */
        {
            /* from red: */
            {
                {  -681,  728, -195 },
                {  -934, 1075,  104 },
                {  -125, -106,  128 },
            },
            /* from green: */
            {
                {   622,  785, 1146 },
                {  1064, 2767,  749 },
                {   854, -634,  402 },
            },
            /* from blue: */
            {
                {  -480, -378, -621 },
                {  -554, 2578,  800 },
                {  -136, -161,-1154 },
            },
        },
        /* Blue: */
        {
            /* from red: */
            {
                {  -374,  142, -154 },
                {  -616,  343, -167 },
                {    31, -185,   64 },
            },
            /* from green: */
            {
                {   150,  281, -258 },
                {   634, 1525, -526 },
                {   510, -475, -422 },
            },
            /* from blue: */
            {
                {   148,  395,  382 },
                {   331, 2628, 1867 },
                {   424,  509,  398 },
            },
        },
    },
    /* Sub-image #3 (1,0): */
    {
        /* Red: */
        {
            /* from red: */
            {
                {    27, -561,  489 },
                {  1747, 5444, -786 },
                {   515, -378,  636 },
            },
            /* from green: */
            {
                {   132,-1105,  722 },
                { -2178, 4913, -208 },
                {  -450,  344, -658 },
            },
            /* from blue: */
            {
                {   244,  128,  174 },
                {    29, -814,-1273 },
                {   228,  118,   73 },
            },
        },
        /* Green: */
        {
            /* from red: */
            {
                { -1010, -857, -127 },
                {   326, 4262,-1431 },
                {  -380, -729,  -28 },
            },
            /* from green: */
            {
                {  1239,-1087, 1149 },
                {  -421, 4518,  311 },
                {  1295,  414,  294 },
            },
            /* from blue: */
            {
                {    21,  297,  -79 },
                {   171,   35,-1163 },
                {   -21,  788, -126 },
            },
        },
        /* Blue: */
        {
            /* from red: */
            {
                {  -644, -804,  -51 },
                {   115, 2347,-1116 },
                {   -87, -641,  -19 },
            },
            /* from green: */
            {
                {   673, -930,   75 },
                {  -305, 2405, -686 },
                {   737,   55, -624 },
            },
            /* from blue: */
            {
                {   326,  716,  357 },
                {   874, 1035,  324 },
                {   842, 1326, 1293 },
            },
        },
    },
    /* Sub-image #4 (1,1): */
    {
        /* Red: */
        {
            /* from red: */
            {
                {   273,  -92,  404 },
                {   535, 3001, 1881 },
                {   315,  423,  428 },
            },
            /* from green: */
            {
                {     8, -584,  -40 },
                { -1047, 3177,  822 },
                {  -604,  372, -580 },
            },
            /* from blue: */
            {
                {   315,  155,  302 },
                { -1053, -376,-1100 },
                {  -156,  740,   38 },
            },
        },
        /* Green: */
        {
            /* from red: */
            {
                {  -653, -513, -326 },
                {  -553, 2051,  778 },
                {  -380,  -66, -320 },
            },
            /* from green: */
            {
                {  1050, -437,  421 },
                {   306, 3276, 1244 },
                {   934,  654,  325 },
            },
            /* from blue: */
            {
                {    65,  279,   67 },
                {  -856,  291, -812 },
                {  -425, 1211,   45 },
            },
        },
        /* Blue: */
        {
            /* from red: */
            {
                {  -363, -586, -212 },
                {  -401,  934,  246 },
                {   -73, -209, -241 },
            },
            /* from green: */
            {
                {   527, -427, -473 },
                {   120, 1759, -196 },
                {   467,  372, -712 },
            },
            /* from blue: */
            {
                {   286,  703,  530 },
                {   102, 1139,  712 },
                {   489, 1531, 1568 },
            },
        },
    },
};

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
                        filters[c][p][0][0] * rgb[(x-1)*3+p + (y-1)*width*3] + filters[c][p][0][1] * rgb[x*3+p + (y-1)*width*3] + filters[c][p][0][2] * rgb[(x+1)*3+p + (y-1)*width*3] +
                        filters[c][p][1][0] * rgb[(x-1)*3+p + (y+0)*width*3] + filters[c][p][1][1] * rgb[x*3+p + (y+0)*width*3] + filters[c][p][1][2] * rgb[(x+1)*3+p + (y+0)*width*3] +
                        filters[c][p][2][0] * rgb[(x-1)*3+p + (y+1)*width*3] + filters[c][p][2][1] * rgb[x*3+p + (y+1)*width*3] + filters[c][p][2][2] * rgb[(x+1)*3+p + (y+1)*width*3];
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

static void copy_subimage(uint16_t* rgbx2, uint16_t* rgb, int dx, int dy)
{
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            for (int ch = 0; ch < 3; ch++)
            {
                rgbx2[(2*x+dx)*3+ch + (2*y+dy)*2*width*3] = rgb[x*3+ch + y*width*3];
            }
        }
    }
}
static void rgb_filter_x2()
{
    int size = width * height * 3 * sizeof(uint16_t);
    uint16_t* rgbf = malloc(size);
    uint16_t* rgbx2 = malloc(size * 4);
    
    memcpy(rgbf, rgb, size);
    rgb_filter_1(rgbf, filters_4k[0]);
    copy_subimage(rgbx2, rgbf, 0, 0);
    
    memcpy(rgbf, rgb, size);
    rgb_filter_1(rgbf, filters_4k[1]);
    copy_subimage(rgbx2, rgbf, 1, 0);

    memcpy(rgbf, rgb, size);
    rgb_filter_1(rgbf, filters_4k[2]);
    copy_subimage(rgbx2, rgbf, 0, 1);

    memcpy(rgbf, rgb, size);
    rgb_filter_1(rgbf, filters_4k[3]);
    copy_subimage(rgbx2, rgbf, 1, 1);
    
    /* replace output buffer with a double-res one */
    free(rgb);
    rgb = rgbx2;
    width *= 2;
    height *= 2;
    
    free(rgbf);
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
        
        if (out_4k)
        {
            printf("Filtering 4K...\n");
            rgb_filter_x2();
        }

        printf("Output file : %s\n", out_filename);
        write_ppm(out_filename, rgb);

        free(rgb); rgb = 0;
    }
    
    printf("Done.\n\n");
    
    return 0;
}
