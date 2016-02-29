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
float exposure = 0;
int filter = 0;
int out_4k = 1;

struct cmd_group options[] = {
    {
        "Processing options", (struct cmd_option[]) {
            { &fixpn,          1,  "--fixpn",        "Fix row and column noise (SLOW, guesswork)" },
            { (void*)&exposure,1,  "--exposure=%f",  "Exposure compensation (EV)" },
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
    /* test footage was recorded with gamma 0.5 */
    /* dark frame median is about 10000 */
    /* clipping point is about 50000 */
    double gain = (65535 - offset) / 40000.0 * powf(2, exposure);

    for (int i = 0; i < width*height*3; i++)
    {
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
            {   454,  904,  422 },
            {   607, 2892,  726 },
            {   367,  170,  524 },
        },
        /* from green: */
        {
            {  -491,  166,  403 },
            {  -665, 2992,  340 },
            {  -508,  -49, -561 },
        },
        /* from blue: */
        {
            {    -5, -468, -137 },
            {  -178,  548, -568 },
            {   100,   54, -493 },
        },
    },
    /* Green: */
    {
        /* from red: */
        {
            {  -691,  370, -343 },
            {  -565, 1991, -165 },
            {  -330, -246, -118 },
        },
        /* from green: */
        {
            {   727,  191,  900 },
            {   833, 3002,  853 },
            {  1002,  107,  307 },
        },
        /* from blue: */
        {
            {  -232, -224, -391 },
            {   -18, 1226, -420 },
            {  -173,  614, -531 },
        },
    },
    /* Blue: */
    {
        /* from red: */
        {
            {  -407,  -55, -217 },
            {  -411,  905, -319 },
            {   -71, -302,  -81 },
        },
        /* from green: */
        {
            {   285, -117, -251 },
            {   488, 1537, -410 },
            {   570,  -69, -560 },
        },
        /* from blue: */
        {
            {   238,  449,  344 },
            {   716, 1784,  949 },
            {   569, 1108,  930 },
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
                {  1065, 1370,   56 },
                {   623, 2112,  153 },
                {   630,  415,  534 },
            },
            /* from green: */
            {
                {  -533,  865,  358 },
                {  -336, 3801, -693 },
                { -1075, -303, -367 },
            },
            /* from blue: */
            {
                {  -342, -577,  -80 },
                {   668, 1089, -114 },
                {  -176,-1170, -437 },
            },
        },
        /* Green: */
        {
            /* from red: */
            {
                {  -350,  800, -697 },
                {  -608, 1332, -592 },
                {   -71,   17,    7 },
            },
            /* from green: */
            {
                {   813,  763,  934 },
                {  1302, 3581,  -38 },
                {   436, -275,  449 },
            },
            /* from blue: */
            {
                {  -529, -263, -407 },
                {   807, 1814, -104 },
                {  -403, -483, -568 },
            },
        },
        /* Blue: */
        {
            /* from red: */
            {
                {  -217,  190, -436 },
                {  -447,  501, -559 },
                {    61, -111,   22 },
            },
            /* from green: */
            {
                {   306,  149, -304 },
                {   826, 1868, -922 },
                {   262, -380, -302 },
            },
            /* from blue: */
            {
                {   201,  496,  424 },
                {  1322, 2203, 1117 },
                {   336,  350,  645 },
            },
        },
    },
    /* Sub-image #2 (0,1): */
    {
        /* Red: */
        {
            /* from red: */
            {
                {   483, 1396,  772 },
                {    21, 1881, 1079 },
                {   365,  307,  752 },
            },
            /* from green: */
            {
                {  -626,  685,  646 },
                {  -257, 2585,  202 },
                {  -396, -872, -335 },
            },
            /* from blue: */
            {
                {  -268, -659, -405 },
                {  -764, 2113,  646 },
                {   178, -684,-1305 },
            },
        },
        /* Green: */
        {
            /* from red: */
            {
                {  -699,  737, -199 },
                {  -959, 1079,   90 },
                {  -155, -109,  107 },
            },
            /* from green: */
            {
                {   622,  802, 1169 },
                {  1089, 2795,  779 },
                {   881, -626,  423 },
            },
            /* from blue: */
            {
                {  -492, -392, -654 },
                {  -551, 2596,  793 },
                {  -128, -161,-1170 },
            },
        },
        /* Blue: */
        {
            /* from red: */
            {
                {  -382,  144, -157 },
                {  -626,  342, -173 },
                {    18, -186,   55 },
            },
            /* from green: */
            {
                {   151,  286, -251 },
                {   645, 1533, -517 },
                {   523, -472, -415 },
            },
            /* from blue: */
            {
                {   144,  392,  371 },
                {   335, 2636, 1866 },
                {   427,  512,  395 },
            },
        },
    },
    /* Sub-image #3 (1,0): */
    {
        /* Red: */
        {
            /* from red: */
            {
                {    15, -560,  485 },
                {  1736, 5454, -800 },
                {   494, -381,  620 },
            },
            /* from green: */
            {
                {   136,-1101,  741 },
                { -2169, 4931, -188 },
                {  -429,  352, -641 },
            },
            /* from blue: */
            {
                {   236,  120,  150 },
                {    34, -819,-1287 },
                {   231,  119,   64 },
            },
        },
        /* Green: */
        {
            /* from red: */
            {
                { -1031, -860, -132 },
                {   309, 4289,-1457 },
                {  -412, -737,  -51 },
            },
            /* from green: */
            {
                {  1245,-1086, 1173 },
                {  -406, 4560,  338 },
                {  1327,  429,  315 },
            },
            /* from blue: */
            {
                {    12,  289, -109 },
                {   179,   33,-1186 },
                {   -13,  796, -134 },
            },
        },
        /* Blue: */
        {
            /* from red: */
            {
                {  -653, -805,  -54 },
                {   106, 2352,-1126 },
                {  -101, -644,  -28 },
            },
            /* from green: */
            {
                {   676, -929,   84 },
                {  -297, 2416, -678 },
                {   752,   60, -617 },
            },
            /* from blue: */
            {
                {   322,  714,  345 },
                {   879, 1037,  319 },
                {   846, 1332, 1293 },
            },
        },
    },
    /* Sub-image #4 (1,1): */
    {
        /* Red: */
        {
            /* from red: */
            {
                {   262,  -90,  400 },
                {   522, 3005, 1873 },
                {   294,  422,  412 },
            },
            /* from green: */
            {
                {    10, -578,  -24 },
                { -1035, 3190,  844 },
                {  -584,  381, -564 },
            },
            /* from blue: */
            {
                {   307,  147,  280 },
                { -1051, -379,-1114 },
                {  -154,  742,   30 },
            },
        },
        /* Green: */
        {
            /* from red: */
            {
                {  -670, -513, -332 },
                {  -575, 2061,  770 },
                {  -411,  -69, -345 },
            },
            /* from green: */
            {
                {  1054, -429,  439 },
                {   326, 3308, 1278 },
                {   962,  672,  346 },
            },
            /* from blue: */
            {
                {    56,  270,   39 },
                {  -856,  291, -832 },
                {  -420, 1221,   39 },
            },
        },
        /* Blue: */
        {
            /* from red: */
            {
                {  -370, -587, -214 },
                {  -410,  935,  241 },
                {   -86, -210, -251 },
            },
            /* from green: */
            {
                {   529, -424, -467 },
                {   129, 1767, -186 },
                {   480,  378, -705 },
            },
            /* from blue: */
            {
                {   282,  701,  519 },
                {   105, 1142,  708 },
                {   492, 1537, 1569 },
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
