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
uint16_t* rgbA = 0;     /* HDMI frame A (width x height x 3, PPM order) */
uint16_t* rgbB = 0;     /* HDMI frame B */
uint16_t* raw = 0;      /* Bayer raw image (double resolution) */
int width;
int height;

/* HDMI dark frames, optional */
uint16_t* darkA;
uint16_t* darkB;
uint16_t* dark;

int filter_size = 5;
int output_stdout = 0;

struct cmd_group options[] = {
    {
        "Options", (struct cmd_option[]) {
           { &output_stdout,     1,      "-",        "Output PGM to stdout (can be piped to raw2dng)" },
           { &filter_size,       3,      "--3x3",    "Use 3x3 filters to recover detail (default 5x5)" },
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

static void change_ext(char* old, char* new, char* newext, int maxsize)
{
    snprintf(new, maxsize - strlen(newext), "%s", old);
    char* ext = strrchr(new, '.');
    if (!ext) ext = new + strlen(new);
    strcpy(ext, newext);
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
static int read_ppm_stream(FILE* fp, uint16_t** prgb)
{
    /* PGM read code from dcraw, adapted for PPM and modified to report EOF */
    int dim[3]={0,0,0}, comment=0, number=0, error=0, nd=0, c, fc;

      if ((fc = fgetc(fp)) != 'P' || fgetc(fp) != '6') error = 1;
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
    
    if (fc == EOF)
    {
        fprintf(stderr, "End of file.\n");
        return 0;
    }
    
    CHECK(!(error || nd < 3), "not a valid PPM file\n");

    width = dim[0];
    height = dim[1];

    CHECK(prgb && !*prgb, "prgb");
    uint16_t* rgb = malloc(width * height * 2 * 3);
    *prgb = rgb;
    
    int size = fread(rgb, 1, width * height * 2 * 3, fp);
    CHECK(size == width * height * 2 * 3, "fread");
    
    /* PPM is big endian, need to reverse it */
    reverse_bytes_order((void*)rgb, width * height * 2 * 3);
    
    return 1;
}

static void read_ppm(char* filename, uint16_t** prgb)
{
    FILE* fp = fopen(filename, "rb");
    CHECK(fp, "could not open %s", filename);
    
    read_ppm_stream(fp, prgb);

    fclose(fp);
}

/* Output 16-bit PGM file */
/* caveat: it reverses bytes order in the buffer */
static void write_pgm_stream(FILE* f, uint16_t * raw, int w, int h)
{
    fprintf(f, "P5\n%d %d\n65535\n", w, h);

    /* PPM is big endian, need to reverse it */
    reverse_bytes_order((void*)raw, w * h * 2);
    fwrite(raw, 1, w * h * 2, f);
}

static void write_pgm(char* filename, uint16_t * raw, int w, int h)
{
    fprintf(stderr, "Writing %s...\n", filename);
    FILE* f = fopen(filename, "wb");
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
    if (!ans) fprintf(stderr, "Not found   : %s\n", filename);
    return ans;
}

static void convert_to_linear(uint16_t * raw, int w, int h)
{
    double gamma = 0.52;
    double gain = 0.85;
    double offset = 45;
    
    int* lut = malloc(0x10000 * sizeof(lut[0]));
    
    for (int i = 0; i < 0x10000; i++)
    {
        double data = i / 65535.0;
        
        /* undo HDMI 16-235 scaling */
        data = data * (235.0 - 16.0) / 255.0 + 16.0 / 255.0;
        
        /* undo gamma applied from our camera, before recording */
        data = pow(data, 1/gamma);
        
        /* scale the (now linear) values to cover the full 12-bit range,
         * with a black level of 128 */
        lut[i] = COERCE(data * 4095 / gain + 128 - offset, 0, 4095);
    }
    
    for (int i = 0; i < w * h; i++)
    {
        raw[i] = lut[raw[i]];
    }
    
    free(lut);
}

static void darkframe_subtract(uint16_t* raw, uint16_t* dark, int w, int h)
{
    for (int i = 0; i < w * h; i++)
    {
        raw[i] = COERCE((int)raw[i] - dark[i] + 128 + 10, 0, 4095);
    }
}

/**
 * Filters to recover Bayer channels (R,G1,G2,B) from two HDMI frames:
 * rgbA: R, G1, B
 * rgbB: R',G2, B'
 * ' = delayed by 1 pixel
 * 
 * The exact placement of pixels might differ.
 * The filters will take care of any differences that may appear,
 * for example, the line swapping bug, which is still present - ping Herbert.
 * 
 * The black box (recorder or ffmpeg or maybe both) also applies a color matrix
 * to the raw data, which will be undone by these filters as well.
 * 
 * These filters were designed to be applied before linearization,
 * on Shogun footage transcoded with ffmpeg -vcodec copy 
 * (output is different with unprocessed MOVs - ffmpeg bug?)
 * 
 * Indices:
 *  - Bayer channel: x%2 + 2*(y%2),
 *  - column parity in the HDMI image: (x/2) % 2,
 *  - HDMI frame parity (A,B),
 *  - predictor channel (R,G,B),
 *  - filter kernel indices (i,j).
 */
const int filters3[4][2][2][3][3][3] = {
  /* Red: */
  [2] = {
    /* even columns: */
    {
      /* from frame A: */
      {
        /* from red (sum=0.08): */
        {
          {    464,  -524,  -122 },
          {   -326,   951,   387 },
          {   -104,    21,   -83 },
        },
        /* from green (sum=0.00): */
        {
          {  -8348,  8083,   -79 },
          {   4527, -3959,   230 },
          {  -3018,  2623,   -30 },
        },
        /* from blue (sum=-0.03): */
        {
          {   7664, -7586,   -30 },
          {  -3884,  3730,    18 },
          {   2734, -2820,   -44 },
        },
      },
      /* from frame B: */
      {
        /* from red (sum=1.01): */
        {
          {   9398, -8421,   -99 },
          {   1177,  5070,   266 },
          {  -3032,  3936,   -42 },
        },
        /* from green (sum=-0.08): */
        {
          {  -9194,  8993,   -11 },
          {  -1766,  1417,   -21 },
          {   2541, -2394,  -190 },
        },
        /* from blue (sum=0.01): */
        {
          {   -347,   259,    90 },
          {    802,  -711,   -37 },
          {    498,  -359,   -93 },
        },
      },
    },
    /* odd columns: */
    {
      /* from frame A: */
      {
        /* from red (sum=0.84): */
        {
          {   -165,  -194,   866 },
          {    192,   263,  5460 },
          {   -149,   -32,   654 },
        },
        /* from green (sum=-0.12): */
        {
          {   -221,  5416, -5716 },
          {    263, 13406,-13528 },
          {   -175, -3649,  3261 },
        },
        /* from blue (sum=-0.01): */
        {
          {     44, -5604,  5608 },
          {     43,-13116, 12935 },
          {    -85,  3325, -3198 },
        },
      },
      /* from frame B: */
      {
        /* from red (sum=0.25): */
        {
          {    215, -2989,  2957 },
          {    526,  8633, -7560 },
          {    144,  1149, -1040 },
        },
        /* from green (sum=0.04): */
        {
          {     29,  2515, -2704 },
          {   -353, -7354,  8103 },
          {    -62,  -601,   763 },
        },
        /* from blue (sum=-0.01): */
        {
          {     -1,   256,  -216 },
          {    -76,   -43,   -64 },
          {     21,  -276,   332 },
        },
      },
    },
  },
  /* Green1: */
  [3] = {
    /* even columns: */
    {
      /* from frame A: */
      {
        /* from red (sum=0.07): */
        {
          {     65,     4,   -66 },
          {    590,  -145,   169 },
          {   -257,   335,  -110 },
        },
        /* from green (sum=0.75): */
        {
          {   5358, -5380,  -148 },
          {  -3296,  9438,   248 },
          {   -291,   344,  -118 },
        },
        /* from blue (sum=0.15): */
        {
          {  -5643,  5667,   -59 },
          {   3132, -2045,   105 },
          {    366,  -371,   101 },
        },
      },
      /* from frame B: */
      {
        /* from red (sum=0.03): */
        {
          {   -359,   326,   -96 },
          {  -6351,  6546,   148 },
          {   3276, -3338,    58 },
        },
        /* from green (sum=0.09): */
        {
          {    524,  -416,    40 },
          {   6370, -6356,   287 },
          {  -3268,  3512,    85 },
        },
        /* from blue (sum=-0.10): */
        {
          {   -236,   128,    31 },
          {   -493,     3,   -92 },
          {   -128,    82,   -73 },
        },
      },
    },
    /* odd columns: */
    {
      /* from frame A: */
      {
        /* from red (sum=0.33): */
        {
          {      3,  -212,   158 },
          {   -100, -1503,  4434 },
          {     63,  -172,    35 },
        },
        /* from green (sum=0.73): */
        {
          {    -16,  5670, -5584 },
          {    217, 20874,-15239 },
          {    -30, -3319,  3400 },
        },
        /* from blue (sum=-0.01): */
        {
          {     22, -4978,  5059 },
          {    140,-11578, 11114 },
          {    -23,  3964, -3788 },
        },
      },
      /* from frame B: */
      {
        /* from red (sum=-0.23): */
        {
          {   -316,  7759, -7823 },
          {  -1557,  4812, -4535 },
          {   -340,   197,  -104 },
        },
        /* from green (sum=0.12): */
        {
          {     38, -7236,  7264 },
          {    107, -4937,  5463 },
          {   -133,  -218,   607 },
        },
        /* from blue (sum=0.07): */
        {
          {     -4,  -473,   467 },
          {     98,   673,  -390 },
          {    -25,   603,  -404 },
        },
      },
    },
  },
  /* Green2: */
  [0] = {
    /* even columns: */
    {
      /* from frame A: */
      {
        /* from red (sum=-0.02): */
        {
          {     17,   -87,   -27 },
          {   -463,   497,   -26 },
          {    189,  -221,    -3 },
        },
        /* from green (sum=0.05): */
        {
          {  -2549,  2702,   -73 },
          {   -277,   537,    40 },
          {   3950, -3976,    47 },
        },
        /* from blue (sum=-0.08): */
        {
          {   2540, -2501,  -130 },
          {    881,  -987,  -292 },
          {  -4222,  4176,  -153 },
        },
      },
      /* from frame B: */
      {
        /* from red (sum=0.11): */
        {
          {    532,  -295,   -48 },
          {   3374, -2774,    35 },
          { -10387, 10458,    16 },
        },
        /* from green (sum=0.80): */
        {
          {   -515,   645,   -56 },
          {  -4905, 11107,   190 },
          {  10386,-10213,  -100 },
        },
        /* from blue (sum=0.14): */
        {
          {   -178,    95,   -34 },
          {   2130,  -686,   -29 },
          {   -161,   121,   -88 },
        },
      },
    },
    /* odd columns: */
    {
      /* from frame A: */
      {
        /* from red (sum=-0.18): */
        {
          {     -8,  -360,   355 },
          {   -113,   361, -1594 },
          {    138,    51,  -302 },
        },
        /* from green (sum=0.09): */
        {
          {     85,  8568, -8264 },
          {    128, -9194,  9167 },
          {     49, -1701,  1878 },
        },
        /* from blue (sum=0.04): */
        {
          {     32, -7766,  7688 },
          {     79,  9079, -8702 },
          {    -74,  1793, -1796 },
        },
      },
      /* from frame B: */
      {
        /* from red (sum=0.27): */
        {
          {   -243, 14363,-14345 },
          {    -68,   685,  2031 },
          {   -133, -2819,  2781 },
        },
        /* from green (sum=0.76): */
        {
          {    -80,-14095, 14148 },
          {    375,  8186, -2135 },
          {   -163,  3080, -3086 },
        },
        /* from blue (sum=0.02): */
        {
          {    -33,   157,   -43 },
          {      9,  -589,   554 },
          {    -19,    66,    52 },
        },
      },
    },
  },
  /* Blue: */
  [1] = {
    /* even columns: */
    {
      /* from frame A: */
      {
        /* from red (sum=0.00): */
        {
          {    567,  -577,   174 },
          {    114,   -44,  -384 },
          {    -72,   260,   -16 },
        },
        /* from green (sum=-0.03): */
        {
          {    617,  -393,   -14 },
          {   8087, -7671,  -710 },
          {    741,  -868,   -47 },
        },
        /* from blue (sum=0.81): */
        {
          {  -1344,  1352,   647 },
          {  -7923,  8321,  4918 },
          {   -735,   608,   793 },
        },
      },
      /* from frame B: */
      {
        /* from red (sum=-0.02): */
        {
          {  -3509,  3389,   -38 },
          {  -2787,  2812,   167 },
          {  14593,-14702,   -61 },
        },
        /* from green (sum=0.00): */
        {
          {   3399, -3717,  -259 },
          {   2445, -2093,   651 },
          { -14566, 14225,   -60 },
        },
        /* from blue (sum=0.23): */
        {
          {    174,  -115,    85 },
          {    566,   104,   707 },
          {    -48,   133,   312 },
        },
      },
    },
    /* odd columns: */
    {
      /* from frame A: */
      {
        /* from red (sum=0.06): */
        {
          {    -51,  -155,   410 },
          {     -8,  -281,   496 },
          {     91,   223,  -236 },
        },
        /* from green (sum=0.04): */
        {
          {    -29,  -688,  1069 },
          {    -60,  1345, -1225 },
          {     18, -1678,  1589 },
        },
        /* from blue (sum=0.13): */
        {
          {    -63,  1469, -1389 },
          {    197,  -240,   956 },
          {   -116,  1615, -1395 },
        },
      },
      /* from frame B: */
      {
        /* from red (sum=-0.08): */
        {
          {   -183,  6555, -6596 },
          {    -85, -4934,  4731 },
          {     20, -5176,  5053 },
        },
        /* from green (sum=-0.07): */
        {
          {    -11, -6824,  6288 },
          {     24,  3998, -3497 },
          {   -126,  5238, -5642 },
        },
        /* from blue (sum=0.92): */
        {
          {   -113,   850,   -39 },
          {    211,  6028,  -225 },
          {   -107,   710,   194 },
        },
      },
    },
  },
};

const int filters5[4][2][2][3][5][5] = {
  /* Red: */
  [2] = {
    /* even columns: */
    {
      /* from frame A: */
      {
        /* from red (sum=0.13): */
        {
          {    -44,    37,   -92,   -27,    55 },
          {    -24,    25,    -5,   -76,   118 },
          {     41,   -51,   626,   485,   -59 },
          {      8,   -47,    83,  -281,   386 },
          {      2,   -15,   -64,    55,   -43 },
        },
        /* from green (sum=0.00): */
        {
          {     51,   423,  -554,    21,    14 },
          {    -65,  -311,   119,   102,  -187 },
          {   -165,   767,    52, -1113,  1334 },
          {    -49,  -427,   152,   553,  -610 },
          {     52,  -335,   186,  -139,   169 },
        },
        /* from blue (sum=0.00): */
        {
          {     -2,  -595,   589,    -0,   -31 },
          {     34,   178,  -109,  -255,   216 },
          {      3,  -205,   140,  1211, -1108 },
          {    -52,   339,  -358,  -323,   382 },
          {      4,   241,  -230,    55,   -97 },
        },
      },
      /* from frame B: */
      {
        /* from red (sum=0.95): */
        {
          {    -60,  1431, -1134,  -702,   605 },
          {    -57,  2249, -1548,  -964,   918 },
          {    164, -5062, 11201,  1443, -1312 },
          {    -44,  4741, -4080, -3411,  3310 },
          {    -61,   168,   124,  2070, -2167 },
        },
        /* from green (sum=-0.08): */
        {
          {     72, -1616,  1535,   687,  -682 },
          {     60, -1980,  1789,   770,  -778 },
          {    -32,  4520, -4730, -1723,  1568 },
          {    -69, -4949,  5214,  3506, -3779 },
          {     66,    69,  -211, -1919,  1981 },
        },
        /* from blue (sum=-0.02): */
        {
          {     -9,   154,  -159,    22,    33 },
          {     19,  -399,   282,   153,  -141 },
          {     -0,   701,  -651,   125,  -210 },
          {     -5,   100,   -58,  -434,   297 },
          {     23,  -273,   218,  -136,   206 },
        },
      },
    },
    /* odd columns: */
    {
      /* from frame A: */
      {
        /* from red (sum=0.89): */
        {
          {    -55,   -52,   113,   284,   -26 },
          {    -64,   -22,  -265,   760,   -71 },
          {    129,   124,   320,  5337,   164 },
          {     76,  -155,  -106,   619,   -40 },
          {    -41,   -39,    -1,   344,   -32 },
        },
        /* from green (sum=-0.10): */
        {
          {    195,  -227,  -253,   230,    12 },
          {    -79,  -128, -1187,   941,    21 },
          {   -963,  1292,  1886, -2074,    23 },
          {    190,  -347,  -295,    67,   -79 },
          {    424,  -446,   -55,   -20,    46 },
        },
        /* from blue (sum=0.00): */
        {
          {   -117,   106,    82,   -52,    39 },
          {    169,  -183,  1139, -1151,     2 },
          {    757,  -711, -1521,  1339,    42 },
          {   -311,   288,   134,   -79,     9 },
          {   -304,   322,   -96,   101,    34 },
        },
      },
      /* from frame B: */
      {
        /* from red (sum=0.20): */
        {
          {    562,  -537,  1735, -1827,   -88 },
          {  -4357,  4551,  1914, -1928,    38 },
          {   6023, -5699,  1324,  -357,   142 },
          {   -646,   757, -4207,  4437,   -28 },
          {   -464,   477,  2445, -2548,   -99 },
        },
        /* from green (sum=0.03): */
        {
          {   -280,   276, -1664,  1580,    25 },
          {   4190, -4151, -2368,  2144,    29 },
          {  -6066,  5781,   356,   466,   -94 },
          {    598,  -697,  4688, -4530,    -4 },
          {    619,  -603, -2830,  2671,    89 },
        },
        /* from blue (sum=-0.02): */
        {
          {   -236,   265,  -236,   222,   -18 },
          {    313,  -266,   181,  -160,    14 },
          {   -165,    39,  -306,   195,   -16 },
          {     -4,   -41,   -45,   157,   -30 },
          {    -73,   101,   129,  -183,     9 },
        },
      },
    },
  },
  /* Green1: */
  [3] = {
    /* even columns: */
    {
      /* from frame A: */
      {
        /* from red (sum=0.05): */
        {
          {    -10,   -90,   155,  -204,   155 },
          {     50,  -220,   231,   155,  -224 },
          {    -35,   878,  -574,   358,  -125 },
          {     -3,  -250,   288,  -244,   141 },
          {     -2,  -117,   171,     8,   -81 },
        },
        /* from green (sum=0.77): */
        {
          {    -10,    37,    47,  -172,   195 },
          {    -23,  -572,   566,  -641,   553 },
          {    156, -2518,  8595,  -508,   720 },
          {    -47,   583,  -558,   380,  -485 },
          {    -16,  -333,   393,  -212,   214 },
        },
        /* from blue (sum=0.16): */
        {
          {     14,    48,   -11,   374,  -387 },
          {      5,   568,  -589,   265,  -358 },
          {    -45,  1919,  -763,   558,  -417 },
          {     43,  -509,   476,  -189,   290 },
          {     -5,   450,  -418,   161,  -196 },
        },
      },
      /* from frame B: */
      {
        /* from red (sum=0.05): */
        {
          {     13,   285,  -329,   402,  -400 },
          {     55,  -296,   261,  -842,   793 },
          {      0,  1036,  -760,  1336, -1231 },
          {     27,  -633,   634, -1981,  2039 },
          {      8,  -325,   301,  1353, -1358 },
        },
        /* from green (sum=0.07): */
        {
          {    -24,  -267,   286,  -566,   544 },
          {      5,   417,  -380,   476,  -471 },
          {    -52, -1011,  1088,  -920,  1113 },
          {     -5,   605,  -372,  2462, -2398 },
          {      6,   294,  -272, -1325,  1360 },
        },
        /* from blue (sum=-0.10): */
        {
          {      6,   -57,    23,   139,  -134 },
          {     19,  -180,    95,   360,  -338 },
          {    -53,  -447,   -24,  -137,    42 },
          {     12,   -62,    16,  -296,   277 },
          {      4,   -27,   -57,   -46,    54 },
        },
      },
    },
    /* odd columns: */
    {
      /* from frame A: */
      {
        /* from red (sum=0.34): */
        {
          {    -55,   113,   237,  -259,    20 },
          {    180,  -211,   -99,     7,    13 },
          {   -155,    83, -2007,  5056,   -21 },
          {   -139,    81,   385,  -525,     9 },
          {    229,  -197,  -189,   193,    17 },
        },
        /* from green (sum=0.74): */
        {
          {    185,  -216,  -505,   548,     1 },
          {   -134,   218,  -191,   286,   -38 },
          {    -56,    80, 11617, -5923,    91 },
          {     98,  -112,  -690,   772,     8 },
          {     -4,    -9,    50,    -7,    -5 },
        },
        /* from blue (sum=-0.02): */
        {
          {   -113,    87,   424,  -347,   -30 },
          {     48,   -43,   689,  -531,    -4 },
          {     71,   -35, -1761,  1295,   -70 },
          {    141,   -95,   634,  -506,    -7 },
          {   -257,   225,   289,  -244,   -54 },
        },
      },
      /* from frame B: */
      {
        /* from red (sum=-0.24): */
        {
          {   1040, -1143, -1168,  1140,    36 },
          {  -1455,  1230,  1443, -1507,   -52 },
          {   3394, -4912, -1508,  1876,   -84 },
          {  -2347,  2058,  3479, -3448,    18 },
          {   -696,   619, -1833,  1812,    37 },
        },
        /* from green (sum=0.11): */
        {
          {  -1090,  1074,  1307, -1309,    -2 },
          {   1046, -1027, -1237,  1216,     7 },
          {  -2947,  3086,  1760, -1263,   -14 },
          {   2557, -2700, -3612,  4033,   -96 },
          {    674,  -608,  1743, -1742,    17 },
        },
        /* from blue (sum=0.08): */
        {
          {     30,   -25,  -135,   114,     4 },
          {    432,  -399,  -215,   223,    21 },
          {   -465,   548,   370,   -82,    11 },
          {   -146,   164,   683,  -394,   -40 },
          {     82,   -62,    77,  -125,    -5 },
        },
      },
    },
  },
  /* Green2: */
  [0] = {
    /* even columns: */
    {
      /* from frame A: */
      {
        /* from red (sum=-0.01): */
        {
          {     -1,   239,  -260,   -41,    50 },
          {    -35,  -338,   367,    -2,   -49 },
          {      6,    71,     5,   129,  -171 },
          {    -13,  -169,   157,  -101,   126 },
          {      7,    88,  -102,    76,   -93 },
        },
        /* from green (sum=0.03): */
        {
          {     26,   155,  -186,   351,  -334 },
          {    -86,  1027,  -833,  -486,   416 },
          {    -46,  -320,   658,   246,  -231 },
          {    -27,   252,  -331,   265,  -271 },
          {     -0,  -271,   265,   130,  -139 },
        },
        /* from blue (sum=-0.07): */
        {
          {      3,  -423,   420,  -388,   295 },
          {     13,  -555,   592,   292,  -410 },
          {    -13,   519,  -552,  -554,   344 },
          {    -11,  -117,   130,  -277,   154 },
          {     10,   153,  -150,  -299,   223 },
        },
      },
      /* from frame B: */
      {
        /* from red (sum=0.10): */
        {
          {     -3,   494,  -520,  -205,   206 },
          {    -21,  -130,   377,  -373,   385 },
          {    -72,  1757, -1148,   332,  -361 },
          {     48, -1194,  1214, -1636,  1673 },
          {     -4,  -955,   976,  1673, -1671 },
        },
        /* from green (sum=0.82): */
        {
          {     -4,  -725,   805,   334,  -321 },
          {     12,    95,   -17,   266,  -301 },
          {     89, -3304,  9438,  -218,   354 },
          {    -12,  1197, -1058,  1603, -1624 },
          {     -6,  1111, -1003, -1741,  1736 },
        },
        /* from blue (sum=0.13): */
        {
          {     12,   202,  -201,  -116,   136 },
          {     12,  -135,    27,   -19,   -56 },
          {    -20,  1950,  -576,    16,   -20 },
          {     -0,  -186,    97,  -102,    34 },
          {      9,  -140,   142,    76,   -55 },
        },
      },
    },
    /* odd columns: */
    {
      /* from frame A: */
      {
        /* from red (sum=-0.21): */
        {
          {   -192,   269,  -126,  -106,    12 },
          {    338,  -376,   172,   -49,    20 },
          {   -158,    45,  -313,  -824,   -89 },
          {    219,  -143,   258,  -549,    -1 },
          {   -165,   201,   -24,  -150,    14 },
        },
        /* from green (sum=0.04): */
        {
          {    365,  -369,  -161,   124,   -13 },
          {   -811,   921,  1108,  -881,    -8 },
          {    422,  -414,    96,   -54,   -45 },
          {   -118,   145,  -346,   398,    32 },
          {    127,  -153,   359,  -350,   -25 },
        },
        /* from blue (sum=0.03): */
        {
          {   -134,   106,   237,  -250,    -7 },
          {    377,  -354,  -860,   792,     6 },
          {   -283,   373,   421,  -147,    53 },
          {    -72,    46,   105,  -116,     2 },
          {      3,   -15,  -294,   293,   -17 },
        },
      },
      /* from frame B: */
      {
        /* from red (sum=0.31): */
        {
          {    176,  -180, -1212,  1263,    28 },
          {    889, -1131,  2059, -2082,   -10 },
          {   -619,   553, -4871,  7696,   -71 },
          {   -157,    57,  5117, -5207,    39 },
          {   -195,   216, -1940,  2020,    64 },
        },
        /* from green (sum=0.80): */
        {
          {   -221,   234,  1272, -1196,     9 },
          {   -848,   777, -1809,  1913,   -23 },
          {    801,  -487, 13730, -7747,   120 },
          {    184,  -265, -5164,  5157,    18 },
          {     65,   -41,  2065, -1929,   -22 },
        },
        /* from blue (sum=0.03): */
        {
          {     32,   -62,    67,   -60,     7 },
          {    -95,    91,   151,   -40,    15 },
          {   -106,   113,  -382,   384,    -9 },
          {    -14,    21,   231,  -147,    31 },
          {     85,  -111,    88,   -53,   -17 },
        },
      },
    },
  },
  /* Blue: */
  [1] = {
    /* even columns: */
    {
      /* from frame A: */
      {
        /* from red (sum=-0.04): */
        {
          {     26,  -134,   176,   139,  -217 },
          {    -14,   447,  -480,   350,  -347 },
          {      9,   296,  -305,  -416,   178 },
          {      5,  -474,   485,   -99,    47 },
          {    -29,   298,  -283,    87,   -70 },
        },
        /* from green (sum=-0.06): */
        {
          {     50,   580,  -712,  -732,   713 },
          {    -10, -1099,  1331, -2187,  2189 },
          {    -48, -1085,  1444,    30,  -753 },
          {     53,   462,  -644,    46,   -55 },
          {     12,  -279,   190,  -971,   983 },
        },
        /* from blue (sum=0.84): */
        {
          {    -27,  -548,   464,   895,  -535 },
          {    -22,   437,  -481,  2574, -1888 },
          {     50,   937,  -666,  4184,   715 },
          {     22,  -134,    14,   794,  -113 },
          {    -18,  -132,    60,  1232,  -905 },
        },
      },
      /* from frame B: */
      {
        /* from red (sum=0.03): */
        {
          {     64,  2408, -2446,    55,   -75 },
          {    -33, -6172,  6255,  1046, -1187 },
          {     -7,  5487, -5375,  -786,  1058 },
          {     39,   347,  -457, -1434,  1383 },
          {     50,   383,  -412, -1554,  1577 },
        },
        /* from green (sum=0.03): */
        {
          {     32, -2287,  2177,  -318,   263 },
          {      3,  5839, -6113, -1466,  1314 },
          {    -76, -5389,  6063,  1695, -1164 },
          {     20,  -343,    68,  1316, -1357 },
          {     40,  -439,   385,  1557, -1557 },
        },
        /* from blue (sum=0.20): */
        {
          {    -49,  -110,    85,   206,  -231 },
          {     22,   369,  -293,   400,  -248 },
          {     78,   170,   471,   191,   331 },
          {    -18,    38,   104,   201,     6 },
          {    -48,   163,  -145,   -48,    -5 },
        },
      },
    },
    /* odd columns: */
    {
      /* from frame A: */
      {
        /* from red (sum=0.04): */
        {
          {    -24,   107,   153,  -301,    28 },
          {    180,  -282,  -368,   762,    12 },
          {    -75,     7,  -781,  1095,   -71 },
          {    140,   -54,   585,  -709,     4 },
          {   -102,   103,   -45,   -25,    24 },
        },
        /* from green (sum=0.01): */
        {
          {     -8,    20,  -771,   623,    39 },
          {   -883,   856,  3152, -2890,     4 },
          {    935, -1035,   621,  -336,  -119 },
          {   -564,   588,  -155,     8,    75 },
          {     24,   -52,   561,  -592,   -10 },
        },
        /* from blue (sum=0.16): */
        {
          {    107,  -177,   510,  -491,    16 },
          {    591,  -579, -2113,  2143,   149 },
          {   -893,  1024,   959,  -498,   387 },
          {    495,  -531,  -441,   615,    62 },
          {     52,  -102,  -513,   539,    16 },
        },
      },
      /* from frame B: */
      {
        /* from red (sum=-0.06): */
        {
          {   -335,   399,   328,  -431,    37 },
          {   -241,    13,  4719, -4744,   -43 },
          {   -530,   404,-10083, 10100,   -55 },
          {   -374,   367,  5216, -5344,    14 },
          {   -903,   966,   357,  -369,    45 },
        },
        /* from green (sum=-0.04): */
        {
          {    156,  -123,   102,  -229,     7 },
          {    383,  -443, -4815,  4578,   -30 },
          {    929,  -858,  9030, -8584,   -27 },
          {    463,  -566, -5256,  4995,   -11 },
          {    538,  -481,  -156,    61,    17 },
        },
        /* from blue (sum=0.88): */
        {
          {    178,  -260,  -242,   545,   -58 },
          {   -286,   205,   667,   -39,   -72 },
          {   -170,   393,  6351,  -829,   231 },
          {   -192,    90,   610,    29,  -100 },
          {    349,  -411,    25,   276,   -61 },
        },
      },
    },
  },
};

static void recover_bayer_channel_3(int dx, int dy, uint16_t* raw, uint16_t* rgbA, uint16_t* rgbB)
{
    int ch = dx%2 + (dy%2) * 2;
    int w = width;
    int h = height;
    
    uint16_t* rgb[2] = {rgbA, rgbB};

    for (int y = 1; y < h-1; y++)
    {
        for (int x = 1; x < w-1; x++)
        {
            int sum = 0;

            /* separate filter for even/odd columns */
            int c = !(x % 2);

            /* we recover each channel from two frames: A and B */
            for (int k = 0; k < 2; k++)
            {
                /* for each channel, we have 3 predictors from each frame */
                for (int p = 0; p < 3; p++)
                {
                    #define filter filters3[ch][c][k][p]
                    #define F(dx,dy) filter[dy+1][dx+1] * rgb[k][(x+dx)*3+p + (y+dy)*w*3]
                    
                    sum +=
                        F(-1,-1) + F(0,-1) + F(1,-1) +
                        F(-1, 0) + F(0, 0) + F(1, 0) +
                        F(-1, 1) + F(0, 1) + F(1, 1) ;
                    
                    #undef F
                    #undef filter
                }
            }

            raw[2*x+dx + (2*y+dy) * (2*width)] = COERCE(sum / 8192, 0, 65535);
        }
    }
}

static void recover_bayer_channel_5(int dx, int dy, uint16_t* raw, uint16_t* rgbA, uint16_t* rgbB)
{
    int ch = dx%2 + (dy%2) * 2;
    int w = width;
    int h = height;
    
    uint16_t* rgb[2] = {rgbA, rgbB};

    for (int y = 2; y < h-2; y++)
    {
        for (int x = 2; x < w-2; x++)
        {
            int sum = 0;

            /* separate filter for even/odd columns */
            int c = !(x % 2);

            /* we recover each channel from two frames: A and B */
            for (int k = 0; k < 2; k++)
            {
                /* for each channel, we have 3 predictors from each frame */
                for (int p = 0; p < 3; p++)
                {
                    #define filter filters5[ch][c][k][p]
                    #define F(dx,dy) filter[dy+2][dx+2] * rgb[k][(x+dx)*3+p + (y+dy)*w*3]
                    
                    sum +=
                        F(-2,-2) + F(-1,-2) + F(0,-2) + F(1,-2) + F(2,-2) +
                        F(-2,-1) + F(-1,-1) + F(0,-1) + F(1,-1) + F(2,-1) +
                        F(-2, 0) + F(-1, 0) + F(0, 0) + F(1, 0) + F(2, 0) +
                        F(-2, 1) + F(-1, 1) + F(0, 1) + F(1, 1) + F(2, 1) +
                        F(-2, 2) + F(-1, 2) + F(0, 2) + F(1, 2) + F(2, 2) ;
                    
                    #undef F
                    #undef filter
                }
            }

            raw[2*x+dx + (2*y+dy) * (2*width)] = COERCE(sum / 8192, 0, 65535);
        }
    }
}

/* recover raw data by filtering the two HDMI frames: rgbA and rgbB */
static void recover_raw_data(uint16_t* raw, uint16_t* rgbA, uint16_t* rgbB)
{
    /* fill border pixels */
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            if (x <= 1 || y <= 1 || x >= width-2 || y >= height-2)
            {
                /* green1 from B, green2 from A, red/blue from B */
                raw[2*x   + (2*y  ) * (2*width)] = rgbB[3*x+1 + y*width*3];
                raw[2*x+1 + (2*y+1) * (2*width)] = rgbA[3*x+1 + y*width*3];
                raw[2*x   + (2*y+1) * (2*width)] = rgbB[3*x   + y*width*3];
                raw[2*x+1 + (2*y  ) * (2*width)] = rgbB[3*x+2 + y*width*3];
            }
        }
    }

    if (filter_size == 3)
    {
        recover_bayer_channel_3(0, 0, raw, rgbA, rgbB);
        recover_bayer_channel_3(0, 1, raw, rgbA, rgbB);
        recover_bayer_channel_3(1, 0, raw, rgbA, rgbB);
        recover_bayer_channel_3(1, 1, raw, rgbA, rgbB);
    }
    else if (filter_size == 5)
    {
        recover_bayer_channel_5(0, 0, raw, rgbA, rgbB);
        recover_bayer_channel_5(0, 1, raw, rgbA, rgbB);
        recover_bayer_channel_5(1, 0, raw, rgbA, rgbB);
        recover_bayer_channel_5(1, 1, raw, rgbA, rgbB);
    }
}

/* return: 1 = ok, 0 = bad, -1 = retry */
int check_frame_order(uint16_t* rgbA, uint16_t* rgbB, uint16_t* rgbC, int k)
{
    /*
     * rgbA: R, G1, B
     * rgbB: R',G2, B'
     * ' = delayed by 1 pixel
     * 
     * They may be reversed, so we will try to autodetect
     * which frame is A and which is B, from the first 3 frames.
     * 
     * We will check the difference between frames 1-2 and 2-3.
     */

    int n = width * height * 3;
    int64_t ab = 0;
    int64_t bc = 0;
    int64_t ac = 0;
    for (int i = 0; i < n; i++)
    {
        ab += ABS((int) rgbA[i] - rgbB[i]);
        bc += ABS((int) rgbB[i] - rgbC[i]);
        ac += ABS((int) rgbA[i] - rgbC[i]);
    }
    
    if (ab == 0 || bc == 0 || ac == 0)
    {
        int a = (bc == 0) ? 1 : 0;
        int b = (ab == 0) ? 1 : 2;
        fprintf(stderr, "Frames %d and %d are identical (cannot check).\n", a+k, b+k);
        return -1;
    }
        
    fprintf(stderr, "Frame deltas : %.3g, %.3g\n", (double) ab, (double) bc);
    
    if (ab > bc)
    {
        fprintf(stderr, "Frame pairs do not match.\n");
        return 0;
    }
    
    return 1;
}

int main(int argc, char** argv)
{
    if (argc == 1)
    {
        fprintf(stderr, "HDMI RAW converter for Axiom BETA\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "  %s clip.mov\n", argv[0]);
        fprintf(stderr, "  raw2dng frame*.pgm [options]\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Calibration files:\n");
        fprintf(stderr, "  hdmi-darkframe-A.ppm, hdmi-darkframe-B.ppm:\n");
        fprintf(stderr, "  averaged dark frames from the HDMI recorder (even/odd frames)\n");
        fprintf(stderr, "\n");
        show_commandline_help(argv[0]);
        return 0;
    }

    /* parse all command-line options */
    for (int k = 1; k < argc; k++)
        if (argv[k][0] == '-')
            parse_commandline_option(argv[k]);
    show_active_options();

    fprintf(stderr, "\n");

    char* dark_filename_a = "darkframe-hdmi-A.ppm";
    char* dark_filename_b = "darkframe-hdmi-B.ppm";
    if (file_exists_warn(dark_filename_a) && file_exists_warn(dark_filename_b))
    {
        fprintf(stderr, "Dark frames : darkframe-hdmi-[AB].ppm\n");
        read_ppm(dark_filename_a, &darkA);
        read_ppm(dark_filename_b, &darkB);

        dark = malloc(width * height * 4 * sizeof(dark[0]));
        recover_raw_data(dark, darkA, darkB);
        convert_to_linear(dark, 2*width, 2*height);
    }

    FILE* pipe = 0;
    
    /* all other arguments are input or output files */
    for (int k = 1; k < argc; k++)
    {
        if (argv[k][0] == '-')
            continue;

        char out_filename[256];

        fprintf(stderr, "\n%s\n", argv[k]);
        
        if (endswith(argv[k], ".mov") || endswith(argv[k], ".MOV"))
        {
            static char fi[256];
            static int frame_count = 1;

            if (!pipe)
            {
                char cmd[1000];
                int skip_frame = 0;
                
                /* read 3 frames to check the frame order (A/B) */
                {
                    snprintf(cmd, sizeof(cmd), "ffmpeg -i '%s' -f image2pipe -vcodec ppm -vframes 3 - -loglevel warning -hide_banner", argv[k]);
                    fprintf(stderr, "%s\n", cmd);
                    FILE* pipe = popen(cmd, "r");
                    CHECK(pipe, "ffmpeg");
                    uint16_t* rgbC = 0;
                    read_ppm_stream(pipe, &rgbA);
                    read_ppm_stream(pipe, &rgbB);
                    read_ppm_stream(pipe, &rgbC);
                    pclose(pipe); pipe = 0;
                    if (check_frame_order(rgbA, rgbB, rgbC, 1) == 0)
                    {
                        skip_frame = 1;
                    }
                    free(rgbA); rgbA = 0;
                    free(rgbB); rgbB = 0;
                    free(rgbC); rgbC = 0;
                }

                /* open the movie again to process all frames */
                fprintf(stderr, "\n");
                snprintf(cmd, sizeof(cmd), "ffmpeg -i '%s' -f image2pipe -vcodec ppm - -nostats", argv[k]);
                fprintf(stderr, "%s\n", cmd);
                pipe = popen(cmd, "r");
                CHECK(pipe, "ffmpeg");

                if (skip_frame)
                {
                    read_ppm_stream(pipe, &rgbA);
                    free(rgbA); rgbA = 0;
                }
            }

            change_ext(argv[k], fi, "", sizeof(fi));
            snprintf(out_filename, sizeof(out_filename), "%s-%05d.pgm", fi, frame_count++);

            /* this may leave rgbA allocated (memory leak),
             * but since the program will exit right away, it's not a huge deal */
            if (!read_ppm_stream(pipe, &rgbA))
                break;
            if (!read_ppm_stream(pipe, &rgbB))
                break;
            
            raw = malloc(width * height * 4 * sizeof(raw[0]));

            /* process the same file at next iteration */
            k--;
        }
        else
        {
            fprintf(stderr, "Unknown file type.\n");
            continue;
        }
        
        fprintf(stderr, "Recovering raw data...\n");
        recover_raw_data(raw, rgbA, rgbB);
        
        fprintf(stderr, "Convert to linear...\n");
        convert_to_linear(raw, 2*width, 2*height);
        
        if (dark)
        {
            fprintf(stderr, "Darkframe subtract...\n");
            darkframe_subtract(raw, dark, 2*width, 2*height);
        }
        
        if (output_stdout)
        {
            write_pgm_stream(stdout, raw, 2*width, 2*height);
        }
        else
        {
            fprintf(stderr, "Output file : %s\n", out_filename);
            write_pgm(out_filename, raw, 2*width, 2*height);
        }

        free(rgbA); rgbA = 0;
        free(rgbB); rgbB = 0;
        free(raw);  raw = 0;
    }
    
    if (pipe) pclose(pipe);
    fprintf(stderr, "Done.\n\n");
    
    return 0;
}
