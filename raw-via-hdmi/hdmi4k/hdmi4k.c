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
#include "omp.h"

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
    
    write_pgm_stream(f, raw, w, h);
    
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

static void recover_bayer_channel_3(int dx, int dy, uint16_t* raw, uint16_t* rgbA, uint16_t* rgbB)
{
    int ch = dx%2 + (dy%2) * 2;
    int w = width;
    int h = height;
    
    uint16_t* rgb[2] = {rgbA, rgbB};

    #pragma omp parallel for
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

/* 5x5 filters are hardcoded - they execute faster that way */
/* Core2Duo: 1.70 plain C, 0.97s OMP, 0.57s OMP + hardcoded filter */
static void recover_bayer_data_5x5_brute_force(uint16_t* raw, uint16_t* rgbA, uint16_t* rgbB)
{
    #pragma omp parallel for
    for (int y = 2; y < height-2; y++)
    {
        for (int x = 2; x < width-2; x++)
        {
            int red = (x % 2)
              ? (  /* odd columns: */
               /* from frame A, red (sum=0.13): */
                   -44*(int)rgbA[x*3+y*5760-11526]    +37*(int)rgbA[x*3+y*5760-11523]    -92*(int)rgbA[x*3+y*5760-11520]    -27*(int)rgbA[x*3+y*5760-11517]    +55*(int)rgbA[x*3+y*5760-11514]
                   -24*(int)rgbA[x*3+y*5760 -5766]    +25*(int)rgbA[x*3+y*5760 -5763]     -5*(int)rgbA[x*3+y*5760 -5760]    -76*(int)rgbA[x*3+y*5760 -5757]   +118*(int)rgbA[x*3+y*5760 -5754]
                   +41*(int)rgbA[x*3+y*5760    -6]    -51*(int)rgbA[x*3+y*5760    -3]   +626*(int)rgbA[x*3+y*5760    +0]   +485*(int)rgbA[x*3+y*5760    +3]    -59*(int)rgbA[x*3+y*5760    +6]
                    +8*(int)rgbA[x*3+y*5760 +5754]    -47*(int)rgbA[x*3+y*5760 +5757]    +83*(int)rgbA[x*3+y*5760 +5760]   -281*(int)rgbA[x*3+y*5760 +5763]   +386*(int)rgbA[x*3+y*5760 +5766]
                    +2*(int)rgbA[x*3+y*5760+11514]    -15*(int)rgbA[x*3+y*5760+11517]    -64*(int)rgbA[x*3+y*5760+11520]    +55*(int)rgbA[x*3+y*5760+11523]    -43*(int)rgbA[x*3+y*5760+11526]
               /* from frame A, green (sum=0.00): */
                   +51*(int)rgbA[x*3+y*5760-11525]   +423*(int)rgbA[x*3+y*5760-11522]   -554*(int)rgbA[x*3+y*5760-11519]    +21*(int)rgbA[x*3+y*5760-11516]    +14*(int)rgbA[x*3+y*5760-11513]
                   -65*(int)rgbA[x*3+y*5760 -5765]   -311*(int)rgbA[x*3+y*5760 -5762]   +119*(int)rgbA[x*3+y*5760 -5759]   +102*(int)rgbA[x*3+y*5760 -5756]   -187*(int)rgbA[x*3+y*5760 -5753]
                  -165*(int)rgbA[x*3+y*5760    -5]   +767*(int)rgbA[x*3+y*5760    -2]    +52*(int)rgbA[x*3+y*5760    +1]  -1113*(int)rgbA[x*3+y*5760    +4]  +1334*(int)rgbA[x*3+y*5760    +7]
                   -49*(int)rgbA[x*3+y*5760 +5755]   -427*(int)rgbA[x*3+y*5760 +5758]   +152*(int)rgbA[x*3+y*5760 +5761]   +553*(int)rgbA[x*3+y*5760 +5764]   -610*(int)rgbA[x*3+y*5760 +5767]
                   +52*(int)rgbA[x*3+y*5760+11515]   -335*(int)rgbA[x*3+y*5760+11518]   +186*(int)rgbA[x*3+y*5760+11521]   -139*(int)rgbA[x*3+y*5760+11524]   +169*(int)rgbA[x*3+y*5760+11527]
               /* from frame A, blue (sum=0.00): */
                    -2*(int)rgbA[x*3+y*5760-11524]   -595*(int)rgbA[x*3+y*5760-11521]   +589*(int)rgbA[x*3+y*5760-11518]     +0*(int)rgbA[x*3+y*5760-11515]    -31*(int)rgbA[x*3+y*5760-11512]
                   +34*(int)rgbA[x*3+y*5760 -5764]   +178*(int)rgbA[x*3+y*5760 -5761]   -109*(int)rgbA[x*3+y*5760 -5758]   -255*(int)rgbA[x*3+y*5760 -5755]   +216*(int)rgbA[x*3+y*5760 -5752]
                    +3*(int)rgbA[x*3+y*5760    -4]   -205*(int)rgbA[x*3+y*5760    -1]   +140*(int)rgbA[x*3+y*5760    +2]  +1211*(int)rgbA[x*3+y*5760    +5]  -1108*(int)rgbA[x*3+y*5760    +8]
                   -52*(int)rgbA[x*3+y*5760 +5756]   +339*(int)rgbA[x*3+y*5760 +5759]   -358*(int)rgbA[x*3+y*5760 +5762]   -323*(int)rgbA[x*3+y*5760 +5765]   +382*(int)rgbA[x*3+y*5760 +5768]
                    +4*(int)rgbA[x*3+y*5760+11516]   +241*(int)rgbA[x*3+y*5760+11519]   -230*(int)rgbA[x*3+y*5760+11522]    +55*(int)rgbA[x*3+y*5760+11525]    -97*(int)rgbA[x*3+y*5760+11528]
               /* from frame B, red (sum=0.95): */
                   -60*(int)rgbB[x*3+y*5760-11526]  +1431*(int)rgbB[x*3+y*5760-11523]  -1134*(int)rgbB[x*3+y*5760-11520]   -702*(int)rgbB[x*3+y*5760-11517]   +605*(int)rgbB[x*3+y*5760-11514]
                   -57*(int)rgbB[x*3+y*5760 -5766]  +2249*(int)rgbB[x*3+y*5760 -5763]  -1548*(int)rgbB[x*3+y*5760 -5760]   -964*(int)rgbB[x*3+y*5760 -5757]   +918*(int)rgbB[x*3+y*5760 -5754]
                  +164*(int)rgbB[x*3+y*5760    -6]  -5062*(int)rgbB[x*3+y*5760    -3] +11201*(int)rgbB[x*3+y*5760    +0]  +1443*(int)rgbB[x*3+y*5760    +3]  -1312*(int)rgbB[x*3+y*5760    +6]
                   -44*(int)rgbB[x*3+y*5760 +5754]  +4741*(int)rgbB[x*3+y*5760 +5757]  -4080*(int)rgbB[x*3+y*5760 +5760]  -3411*(int)rgbB[x*3+y*5760 +5763]  +3310*(int)rgbB[x*3+y*5760 +5766]
                   -61*(int)rgbB[x*3+y*5760+11514]   +168*(int)rgbB[x*3+y*5760+11517]   +124*(int)rgbB[x*3+y*5760+11520]  +2070*(int)rgbB[x*3+y*5760+11523]  -2167*(int)rgbB[x*3+y*5760+11526]
               /* from frame B, green (sum=-0.08): */
                   +72*(int)rgbB[x*3+y*5760-11525]  -1616*(int)rgbB[x*3+y*5760-11522]  +1535*(int)rgbB[x*3+y*5760-11519]   +687*(int)rgbB[x*3+y*5760-11516]   -682*(int)rgbB[x*3+y*5760-11513]
                   +60*(int)rgbB[x*3+y*5760 -5765]  -1980*(int)rgbB[x*3+y*5760 -5762]  +1789*(int)rgbB[x*3+y*5760 -5759]   +770*(int)rgbB[x*3+y*5760 -5756]   -778*(int)rgbB[x*3+y*5760 -5753]
                   -32*(int)rgbB[x*3+y*5760    -5]  +4520*(int)rgbB[x*3+y*5760    -2]  -4730*(int)rgbB[x*3+y*5760    +1]  -1723*(int)rgbB[x*3+y*5760    +4]  +1568*(int)rgbB[x*3+y*5760    +7]
                   -69*(int)rgbB[x*3+y*5760 +5755]  -4949*(int)rgbB[x*3+y*5760 +5758]  +5214*(int)rgbB[x*3+y*5760 +5761]  +3506*(int)rgbB[x*3+y*5760 +5764]  -3779*(int)rgbB[x*3+y*5760 +5767]
                   +66*(int)rgbB[x*3+y*5760+11515]    +69*(int)rgbB[x*3+y*5760+11518]   -211*(int)rgbB[x*3+y*5760+11521]  -1919*(int)rgbB[x*3+y*5760+11524]  +1981*(int)rgbB[x*3+y*5760+11527]
               /* from frame B, blue (sum=-0.02): */
                    -9*(int)rgbB[x*3+y*5760-11524]   +154*(int)rgbB[x*3+y*5760-11521]   -159*(int)rgbB[x*3+y*5760-11518]    +22*(int)rgbB[x*3+y*5760-11515]    +33*(int)rgbB[x*3+y*5760-11512]
                   +19*(int)rgbB[x*3+y*5760 -5764]   -399*(int)rgbB[x*3+y*5760 -5761]   +282*(int)rgbB[x*3+y*5760 -5758]   +153*(int)rgbB[x*3+y*5760 -5755]   -141*(int)rgbB[x*3+y*5760 -5752]
                    +0*(int)rgbB[x*3+y*5760    -4]   +701*(int)rgbB[x*3+y*5760    -1]   -651*(int)rgbB[x*3+y*5760    +2]   +125*(int)rgbB[x*3+y*5760    +5]   -210*(int)rgbB[x*3+y*5760    +8]
                    -5*(int)rgbB[x*3+y*5760 +5756]   +100*(int)rgbB[x*3+y*5760 +5759]    -58*(int)rgbB[x*3+y*5760 +5762]   -434*(int)rgbB[x*3+y*5760 +5765]   +297*(int)rgbB[x*3+y*5760 +5768]
                   +23*(int)rgbB[x*3+y*5760+11516]   -273*(int)rgbB[x*3+y*5760+11519]   +218*(int)rgbB[x*3+y*5760+11522]   -136*(int)rgbB[x*3+y*5760+11525]   +206*(int)rgbB[x*3+y*5760+11528]
            ) : (  /* even columns: */
               /* from frame A, red (sum=0.89): */
                   -55*(int)rgbA[x*3+y*5760-11526]    -52*(int)rgbA[x*3+y*5760-11523]   +113*(int)rgbA[x*3+y*5760-11520]   +284*(int)rgbA[x*3+y*5760-11517]    -26*(int)rgbA[x*3+y*5760-11514]
                   -64*(int)rgbA[x*3+y*5760 -5766]    -22*(int)rgbA[x*3+y*5760 -5763]   -265*(int)rgbA[x*3+y*5760 -5760]   +760*(int)rgbA[x*3+y*5760 -5757]    -71*(int)rgbA[x*3+y*5760 -5754]
                  +129*(int)rgbA[x*3+y*5760    -6]   +124*(int)rgbA[x*3+y*5760    -3]   +320*(int)rgbA[x*3+y*5760    +0]  +5337*(int)rgbA[x*3+y*5760    +3]   +164*(int)rgbA[x*3+y*5760    +6]
                   +76*(int)rgbA[x*3+y*5760 +5754]   -155*(int)rgbA[x*3+y*5760 +5757]   -106*(int)rgbA[x*3+y*5760 +5760]   +619*(int)rgbA[x*3+y*5760 +5763]    -40*(int)rgbA[x*3+y*5760 +5766]
                   -41*(int)rgbA[x*3+y*5760+11514]    -39*(int)rgbA[x*3+y*5760+11517]     -1*(int)rgbA[x*3+y*5760+11520]   +344*(int)rgbA[x*3+y*5760+11523]    -32*(int)rgbA[x*3+y*5760+11526]
               /* from frame A, green (sum=-0.10): */
                  +195*(int)rgbA[x*3+y*5760-11525]   -227*(int)rgbA[x*3+y*5760-11522]   -253*(int)rgbA[x*3+y*5760-11519]   +230*(int)rgbA[x*3+y*5760-11516]    +12*(int)rgbA[x*3+y*5760-11513]
                   -79*(int)rgbA[x*3+y*5760 -5765]   -128*(int)rgbA[x*3+y*5760 -5762]  -1187*(int)rgbA[x*3+y*5760 -5759]   +941*(int)rgbA[x*3+y*5760 -5756]    +21*(int)rgbA[x*3+y*5760 -5753]
                  -963*(int)rgbA[x*3+y*5760    -5]  +1292*(int)rgbA[x*3+y*5760    -2]  +1886*(int)rgbA[x*3+y*5760    +1]  -2074*(int)rgbA[x*3+y*5760    +4]    +23*(int)rgbA[x*3+y*5760    +7]
                  +190*(int)rgbA[x*3+y*5760 +5755]   -347*(int)rgbA[x*3+y*5760 +5758]   -295*(int)rgbA[x*3+y*5760 +5761]    +67*(int)rgbA[x*3+y*5760 +5764]    -79*(int)rgbA[x*3+y*5760 +5767]
                  +424*(int)rgbA[x*3+y*5760+11515]   -446*(int)rgbA[x*3+y*5760+11518]    -55*(int)rgbA[x*3+y*5760+11521]    -20*(int)rgbA[x*3+y*5760+11524]    +46*(int)rgbA[x*3+y*5760+11527]
               /* from frame A, blue (sum=0.00): */
                  -117*(int)rgbA[x*3+y*5760-11524]   +106*(int)rgbA[x*3+y*5760-11521]    +82*(int)rgbA[x*3+y*5760-11518]    -52*(int)rgbA[x*3+y*5760-11515]    +39*(int)rgbA[x*3+y*5760-11512]
                  +169*(int)rgbA[x*3+y*5760 -5764]   -183*(int)rgbA[x*3+y*5760 -5761]  +1139*(int)rgbA[x*3+y*5760 -5758]  -1151*(int)rgbA[x*3+y*5760 -5755]     +2*(int)rgbA[x*3+y*5760 -5752]
                  +757*(int)rgbA[x*3+y*5760    -4]   -711*(int)rgbA[x*3+y*5760    -1]  -1521*(int)rgbA[x*3+y*5760    +2]  +1339*(int)rgbA[x*3+y*5760    +5]    +42*(int)rgbA[x*3+y*5760    +8]
                  -311*(int)rgbA[x*3+y*5760 +5756]   +288*(int)rgbA[x*3+y*5760 +5759]   +134*(int)rgbA[x*3+y*5760 +5762]    -79*(int)rgbA[x*3+y*5760 +5765]     +9*(int)rgbA[x*3+y*5760 +5768]
                  -304*(int)rgbA[x*3+y*5760+11516]   +322*(int)rgbA[x*3+y*5760+11519]    -96*(int)rgbA[x*3+y*5760+11522]   +101*(int)rgbA[x*3+y*5760+11525]    +34*(int)rgbA[x*3+y*5760+11528]
               /* from frame B, red (sum=0.20): */
                  +562*(int)rgbB[x*3+y*5760-11526]   -537*(int)rgbB[x*3+y*5760-11523]  +1735*(int)rgbB[x*3+y*5760-11520]  -1827*(int)rgbB[x*3+y*5760-11517]    -88*(int)rgbB[x*3+y*5760-11514]
                 -4357*(int)rgbB[x*3+y*5760 -5766]  +4551*(int)rgbB[x*3+y*5760 -5763]  +1914*(int)rgbB[x*3+y*5760 -5760]  -1928*(int)rgbB[x*3+y*5760 -5757]    +38*(int)rgbB[x*3+y*5760 -5754]
                 +6023*(int)rgbB[x*3+y*5760    -6]  -5699*(int)rgbB[x*3+y*5760    -3]  +1324*(int)rgbB[x*3+y*5760    +0]   -357*(int)rgbB[x*3+y*5760    +3]   +142*(int)rgbB[x*3+y*5760    +6]
                  -646*(int)rgbB[x*3+y*5760 +5754]   +757*(int)rgbB[x*3+y*5760 +5757]  -4207*(int)rgbB[x*3+y*5760 +5760]  +4437*(int)rgbB[x*3+y*5760 +5763]    -28*(int)rgbB[x*3+y*5760 +5766]
                  -464*(int)rgbB[x*3+y*5760+11514]   +477*(int)rgbB[x*3+y*5760+11517]  +2445*(int)rgbB[x*3+y*5760+11520]  -2548*(int)rgbB[x*3+y*5760+11523]    -99*(int)rgbB[x*3+y*5760+11526]
               /* from frame B, green (sum=0.03): */
                  -280*(int)rgbB[x*3+y*5760-11525]   +276*(int)rgbB[x*3+y*5760-11522]  -1664*(int)rgbB[x*3+y*5760-11519]  +1580*(int)rgbB[x*3+y*5760-11516]    +25*(int)rgbB[x*3+y*5760-11513]
                 +4190*(int)rgbB[x*3+y*5760 -5765]  -4151*(int)rgbB[x*3+y*5760 -5762]  -2368*(int)rgbB[x*3+y*5760 -5759]  +2144*(int)rgbB[x*3+y*5760 -5756]    +29*(int)rgbB[x*3+y*5760 -5753]
                 -6066*(int)rgbB[x*3+y*5760    -5]  +5781*(int)rgbB[x*3+y*5760    -2]   +356*(int)rgbB[x*3+y*5760    +1]   +466*(int)rgbB[x*3+y*5760    +4]    -94*(int)rgbB[x*3+y*5760    +7]
                  +598*(int)rgbB[x*3+y*5760 +5755]   -697*(int)rgbB[x*3+y*5760 +5758]  +4688*(int)rgbB[x*3+y*5760 +5761]  -4530*(int)rgbB[x*3+y*5760 +5764]     -4*(int)rgbB[x*3+y*5760 +5767]
                  +619*(int)rgbB[x*3+y*5760+11515]   -603*(int)rgbB[x*3+y*5760+11518]  -2830*(int)rgbB[x*3+y*5760+11521]  +2671*(int)rgbB[x*3+y*5760+11524]    +89*(int)rgbB[x*3+y*5760+11527]
               /* from frame B, blue (sum=-0.02): */
                  -236*(int)rgbB[x*3+y*5760-11524]   +265*(int)rgbB[x*3+y*5760-11521]   -236*(int)rgbB[x*3+y*5760-11518]   +222*(int)rgbB[x*3+y*5760-11515]    -18*(int)rgbB[x*3+y*5760-11512]
                  +313*(int)rgbB[x*3+y*5760 -5764]   -266*(int)rgbB[x*3+y*5760 -5761]   +181*(int)rgbB[x*3+y*5760 -5758]   -160*(int)rgbB[x*3+y*5760 -5755]    +14*(int)rgbB[x*3+y*5760 -5752]
                  -165*(int)rgbB[x*3+y*5760    -4]    +39*(int)rgbB[x*3+y*5760    -1]   -306*(int)rgbB[x*3+y*5760    +2]   +195*(int)rgbB[x*3+y*5760    +5]    -16*(int)rgbB[x*3+y*5760    +8]
                    -4*(int)rgbB[x*3+y*5760 +5756]    -41*(int)rgbB[x*3+y*5760 +5759]    -45*(int)rgbB[x*3+y*5760 +5762]   +157*(int)rgbB[x*3+y*5760 +5765]    -30*(int)rgbB[x*3+y*5760 +5768]
                   -73*(int)rgbB[x*3+y*5760+11516]   +101*(int)rgbB[x*3+y*5760+11519]   +129*(int)rgbB[x*3+y*5760+11522]   -183*(int)rgbB[x*3+y*5760+11525]     +9*(int)rgbB[x*3+y*5760+11528]
            );
            int green1 = (x % 2)
              ? (  /* odd columns: */
               /* from frame A, red (sum=0.05): */
                   -10*(int)rgbA[x*3+y*5760-11526]    -90*(int)rgbA[x*3+y*5760-11523]   +155*(int)rgbA[x*3+y*5760-11520]   -204*(int)rgbA[x*3+y*5760-11517]   +155*(int)rgbA[x*3+y*5760-11514]
                   +50*(int)rgbA[x*3+y*5760 -5766]   -220*(int)rgbA[x*3+y*5760 -5763]   +231*(int)rgbA[x*3+y*5760 -5760]   +155*(int)rgbA[x*3+y*5760 -5757]   -224*(int)rgbA[x*3+y*5760 -5754]
                   -35*(int)rgbA[x*3+y*5760    -6]   +878*(int)rgbA[x*3+y*5760    -3]   -574*(int)rgbA[x*3+y*5760    +0]   +358*(int)rgbA[x*3+y*5760    +3]   -125*(int)rgbA[x*3+y*5760    +6]
                    -3*(int)rgbA[x*3+y*5760 +5754]   -250*(int)rgbA[x*3+y*5760 +5757]   +288*(int)rgbA[x*3+y*5760 +5760]   -244*(int)rgbA[x*3+y*5760 +5763]   +141*(int)rgbA[x*3+y*5760 +5766]
                    -2*(int)rgbA[x*3+y*5760+11514]   -117*(int)rgbA[x*3+y*5760+11517]   +171*(int)rgbA[x*3+y*5760+11520]     +8*(int)rgbA[x*3+y*5760+11523]    -81*(int)rgbA[x*3+y*5760+11526]
               /* from frame A, green (sum=0.77): */
                   -10*(int)rgbA[x*3+y*5760-11525]    +37*(int)rgbA[x*3+y*5760-11522]    +47*(int)rgbA[x*3+y*5760-11519]   -172*(int)rgbA[x*3+y*5760-11516]   +195*(int)rgbA[x*3+y*5760-11513]
                   -23*(int)rgbA[x*3+y*5760 -5765]   -572*(int)rgbA[x*3+y*5760 -5762]   +566*(int)rgbA[x*3+y*5760 -5759]   -641*(int)rgbA[x*3+y*5760 -5756]   +553*(int)rgbA[x*3+y*5760 -5753]
                  +156*(int)rgbA[x*3+y*5760    -5]  -2518*(int)rgbA[x*3+y*5760    -2]  +8595*(int)rgbA[x*3+y*5760    +1]   -508*(int)rgbA[x*3+y*5760    +4]   +720*(int)rgbA[x*3+y*5760    +7]
                   -47*(int)rgbA[x*3+y*5760 +5755]   +583*(int)rgbA[x*3+y*5760 +5758]   -558*(int)rgbA[x*3+y*5760 +5761]   +380*(int)rgbA[x*3+y*5760 +5764]   -485*(int)rgbA[x*3+y*5760 +5767]
                   -16*(int)rgbA[x*3+y*5760+11515]   -333*(int)rgbA[x*3+y*5760+11518]   +393*(int)rgbA[x*3+y*5760+11521]   -212*(int)rgbA[x*3+y*5760+11524]   +214*(int)rgbA[x*3+y*5760+11527]
               /* from frame A, blue (sum=0.16): */
                   +14*(int)rgbA[x*3+y*5760-11524]    +48*(int)rgbA[x*3+y*5760-11521]    -11*(int)rgbA[x*3+y*5760-11518]   +374*(int)rgbA[x*3+y*5760-11515]   -387*(int)rgbA[x*3+y*5760-11512]
                    +5*(int)rgbA[x*3+y*5760 -5764]   +568*(int)rgbA[x*3+y*5760 -5761]   -589*(int)rgbA[x*3+y*5760 -5758]   +265*(int)rgbA[x*3+y*5760 -5755]   -358*(int)rgbA[x*3+y*5760 -5752]
                   -45*(int)rgbA[x*3+y*5760    -4]  +1919*(int)rgbA[x*3+y*5760    -1]   -763*(int)rgbA[x*3+y*5760    +2]   +558*(int)rgbA[x*3+y*5760    +5]   -417*(int)rgbA[x*3+y*5760    +8]
                   +43*(int)rgbA[x*3+y*5760 +5756]   -509*(int)rgbA[x*3+y*5760 +5759]   +476*(int)rgbA[x*3+y*5760 +5762]   -189*(int)rgbA[x*3+y*5760 +5765]   +290*(int)rgbA[x*3+y*5760 +5768]
                    -5*(int)rgbA[x*3+y*5760+11516]   +450*(int)rgbA[x*3+y*5760+11519]   -418*(int)rgbA[x*3+y*5760+11522]   +161*(int)rgbA[x*3+y*5760+11525]   -196*(int)rgbA[x*3+y*5760+11528]
               /* from frame B, red (sum=0.05): */
                   +13*(int)rgbB[x*3+y*5760-11526]   +285*(int)rgbB[x*3+y*5760-11523]   -329*(int)rgbB[x*3+y*5760-11520]   +402*(int)rgbB[x*3+y*5760-11517]   -400*(int)rgbB[x*3+y*5760-11514]
                   +55*(int)rgbB[x*3+y*5760 -5766]   -296*(int)rgbB[x*3+y*5760 -5763]   +261*(int)rgbB[x*3+y*5760 -5760]   -842*(int)rgbB[x*3+y*5760 -5757]   +793*(int)rgbB[x*3+y*5760 -5754]
                    +0*(int)rgbB[x*3+y*5760    -6]  +1036*(int)rgbB[x*3+y*5760    -3]   -760*(int)rgbB[x*3+y*5760    +0]  +1336*(int)rgbB[x*3+y*5760    +3]  -1231*(int)rgbB[x*3+y*5760    +6]
                   +27*(int)rgbB[x*3+y*5760 +5754]   -633*(int)rgbB[x*3+y*5760 +5757]   +634*(int)rgbB[x*3+y*5760 +5760]  -1981*(int)rgbB[x*3+y*5760 +5763]  +2039*(int)rgbB[x*3+y*5760 +5766]
                    +8*(int)rgbB[x*3+y*5760+11514]   -325*(int)rgbB[x*3+y*5760+11517]   +301*(int)rgbB[x*3+y*5760+11520]  +1353*(int)rgbB[x*3+y*5760+11523]  -1358*(int)rgbB[x*3+y*5760+11526]
               /* from frame B, green (sum=0.07): */
                   -24*(int)rgbB[x*3+y*5760-11525]   -267*(int)rgbB[x*3+y*5760-11522]   +286*(int)rgbB[x*3+y*5760-11519]   -566*(int)rgbB[x*3+y*5760-11516]   +544*(int)rgbB[x*3+y*5760-11513]
                    +5*(int)rgbB[x*3+y*5760 -5765]   +417*(int)rgbB[x*3+y*5760 -5762]   -380*(int)rgbB[x*3+y*5760 -5759]   +476*(int)rgbB[x*3+y*5760 -5756]   -471*(int)rgbB[x*3+y*5760 -5753]
                   -52*(int)rgbB[x*3+y*5760    -5]  -1011*(int)rgbB[x*3+y*5760    -2]  +1088*(int)rgbB[x*3+y*5760    +1]   -920*(int)rgbB[x*3+y*5760    +4]  +1113*(int)rgbB[x*3+y*5760    +7]
                    -5*(int)rgbB[x*3+y*5760 +5755]   +605*(int)rgbB[x*3+y*5760 +5758]   -372*(int)rgbB[x*3+y*5760 +5761]  +2462*(int)rgbB[x*3+y*5760 +5764]  -2398*(int)rgbB[x*3+y*5760 +5767]
                    +6*(int)rgbB[x*3+y*5760+11515]   +294*(int)rgbB[x*3+y*5760+11518]   -272*(int)rgbB[x*3+y*5760+11521]  -1325*(int)rgbB[x*3+y*5760+11524]  +1360*(int)rgbB[x*3+y*5760+11527]
               /* from frame B, blue (sum=-0.10): */
                    +6*(int)rgbB[x*3+y*5760-11524]    -57*(int)rgbB[x*3+y*5760-11521]    +23*(int)rgbB[x*3+y*5760-11518]   +139*(int)rgbB[x*3+y*5760-11515]   -134*(int)rgbB[x*3+y*5760-11512]
                   +19*(int)rgbB[x*3+y*5760 -5764]   -180*(int)rgbB[x*3+y*5760 -5761]    +95*(int)rgbB[x*3+y*5760 -5758]   +360*(int)rgbB[x*3+y*5760 -5755]   -338*(int)rgbB[x*3+y*5760 -5752]
                   -53*(int)rgbB[x*3+y*5760    -4]   -447*(int)rgbB[x*3+y*5760    -1]    -24*(int)rgbB[x*3+y*5760    +2]   -137*(int)rgbB[x*3+y*5760    +5]    +42*(int)rgbB[x*3+y*5760    +8]
                   +12*(int)rgbB[x*3+y*5760 +5756]    -62*(int)rgbB[x*3+y*5760 +5759]    +16*(int)rgbB[x*3+y*5760 +5762]   -296*(int)rgbB[x*3+y*5760 +5765]   +277*(int)rgbB[x*3+y*5760 +5768]
                    +4*(int)rgbB[x*3+y*5760+11516]    -27*(int)rgbB[x*3+y*5760+11519]    -57*(int)rgbB[x*3+y*5760+11522]    -46*(int)rgbB[x*3+y*5760+11525]    +54*(int)rgbB[x*3+y*5760+11528]
            ) : (  /* even columns: */
               /* from frame A, red (sum=0.34): */
                   -55*(int)rgbA[x*3+y*5760-11526]   +113*(int)rgbA[x*3+y*5760-11523]   +237*(int)rgbA[x*3+y*5760-11520]   -259*(int)rgbA[x*3+y*5760-11517]    +20*(int)rgbA[x*3+y*5760-11514]
                  +180*(int)rgbA[x*3+y*5760 -5766]   -211*(int)rgbA[x*3+y*5760 -5763]    -99*(int)rgbA[x*3+y*5760 -5760]     +7*(int)rgbA[x*3+y*5760 -5757]    +13*(int)rgbA[x*3+y*5760 -5754]
                  -155*(int)rgbA[x*3+y*5760    -6]    +83*(int)rgbA[x*3+y*5760    -3]  -2007*(int)rgbA[x*3+y*5760    +0]  +5056*(int)rgbA[x*3+y*5760    +3]    -21*(int)rgbA[x*3+y*5760    +6]
                  -139*(int)rgbA[x*3+y*5760 +5754]    +81*(int)rgbA[x*3+y*5760 +5757]   +385*(int)rgbA[x*3+y*5760 +5760]   -525*(int)rgbA[x*3+y*5760 +5763]     +9*(int)rgbA[x*3+y*5760 +5766]
                  +229*(int)rgbA[x*3+y*5760+11514]   -197*(int)rgbA[x*3+y*5760+11517]   -189*(int)rgbA[x*3+y*5760+11520]   +193*(int)rgbA[x*3+y*5760+11523]    +17*(int)rgbA[x*3+y*5760+11526]
               /* from frame A, green (sum=0.74): */
                  +185*(int)rgbA[x*3+y*5760-11525]   -216*(int)rgbA[x*3+y*5760-11522]   -505*(int)rgbA[x*3+y*5760-11519]   +548*(int)rgbA[x*3+y*5760-11516]     +1*(int)rgbA[x*3+y*5760-11513]
                  -134*(int)rgbA[x*3+y*5760 -5765]   +218*(int)rgbA[x*3+y*5760 -5762]   -191*(int)rgbA[x*3+y*5760 -5759]   +286*(int)rgbA[x*3+y*5760 -5756]    -38*(int)rgbA[x*3+y*5760 -5753]
                   -56*(int)rgbA[x*3+y*5760    -5]    +80*(int)rgbA[x*3+y*5760    -2] +11617*(int)rgbA[x*3+y*5760    +1]  -5923*(int)rgbA[x*3+y*5760    +4]    +91*(int)rgbA[x*3+y*5760    +7]
                   +98*(int)rgbA[x*3+y*5760 +5755]   -112*(int)rgbA[x*3+y*5760 +5758]   -690*(int)rgbA[x*3+y*5760 +5761]   +772*(int)rgbA[x*3+y*5760 +5764]     +8*(int)rgbA[x*3+y*5760 +5767]
                    -4*(int)rgbA[x*3+y*5760+11515]     -9*(int)rgbA[x*3+y*5760+11518]    +50*(int)rgbA[x*3+y*5760+11521]     -7*(int)rgbA[x*3+y*5760+11524]     -5*(int)rgbA[x*3+y*5760+11527]
               /* from frame A, blue (sum=-0.02): */
                  -113*(int)rgbA[x*3+y*5760-11524]    +87*(int)rgbA[x*3+y*5760-11521]   +424*(int)rgbA[x*3+y*5760-11518]   -347*(int)rgbA[x*3+y*5760-11515]    -30*(int)rgbA[x*3+y*5760-11512]
                   +48*(int)rgbA[x*3+y*5760 -5764]    -43*(int)rgbA[x*3+y*5760 -5761]   +689*(int)rgbA[x*3+y*5760 -5758]   -531*(int)rgbA[x*3+y*5760 -5755]     -4*(int)rgbA[x*3+y*5760 -5752]
                   +71*(int)rgbA[x*3+y*5760    -4]    -35*(int)rgbA[x*3+y*5760    -1]  -1761*(int)rgbA[x*3+y*5760    +2]  +1295*(int)rgbA[x*3+y*5760    +5]    -70*(int)rgbA[x*3+y*5760    +8]
                  +141*(int)rgbA[x*3+y*5760 +5756]    -95*(int)rgbA[x*3+y*5760 +5759]   +634*(int)rgbA[x*3+y*5760 +5762]   -506*(int)rgbA[x*3+y*5760 +5765]     -7*(int)rgbA[x*3+y*5760 +5768]
                  -257*(int)rgbA[x*3+y*5760+11516]   +225*(int)rgbA[x*3+y*5760+11519]   +289*(int)rgbA[x*3+y*5760+11522]   -244*(int)rgbA[x*3+y*5760+11525]    -54*(int)rgbA[x*3+y*5760+11528]
               /* from frame B, red (sum=-0.24): */
                 +1040*(int)rgbB[x*3+y*5760-11526]  -1143*(int)rgbB[x*3+y*5760-11523]  -1168*(int)rgbB[x*3+y*5760-11520]  +1140*(int)rgbB[x*3+y*5760-11517]    +36*(int)rgbB[x*3+y*5760-11514]
                 -1455*(int)rgbB[x*3+y*5760 -5766]  +1230*(int)rgbB[x*3+y*5760 -5763]  +1443*(int)rgbB[x*3+y*5760 -5760]  -1507*(int)rgbB[x*3+y*5760 -5757]    -52*(int)rgbB[x*3+y*5760 -5754]
                 +3394*(int)rgbB[x*3+y*5760    -6]  -4912*(int)rgbB[x*3+y*5760    -3]  -1508*(int)rgbB[x*3+y*5760    +0]  +1876*(int)rgbB[x*3+y*5760    +3]    -84*(int)rgbB[x*3+y*5760    +6]
                 -2347*(int)rgbB[x*3+y*5760 +5754]  +2058*(int)rgbB[x*3+y*5760 +5757]  +3479*(int)rgbB[x*3+y*5760 +5760]  -3448*(int)rgbB[x*3+y*5760 +5763]    +18*(int)rgbB[x*3+y*5760 +5766]
                  -696*(int)rgbB[x*3+y*5760+11514]   +619*(int)rgbB[x*3+y*5760+11517]  -1833*(int)rgbB[x*3+y*5760+11520]  +1812*(int)rgbB[x*3+y*5760+11523]    +37*(int)rgbB[x*3+y*5760+11526]
               /* from frame B, green (sum=0.11): */
                 -1090*(int)rgbB[x*3+y*5760-11525]  +1074*(int)rgbB[x*3+y*5760-11522]  +1307*(int)rgbB[x*3+y*5760-11519]  -1309*(int)rgbB[x*3+y*5760-11516]     -2*(int)rgbB[x*3+y*5760-11513]
                 +1046*(int)rgbB[x*3+y*5760 -5765]  -1027*(int)rgbB[x*3+y*5760 -5762]  -1237*(int)rgbB[x*3+y*5760 -5759]  +1216*(int)rgbB[x*3+y*5760 -5756]     +7*(int)rgbB[x*3+y*5760 -5753]
                 -2947*(int)rgbB[x*3+y*5760    -5]  +3086*(int)rgbB[x*3+y*5760    -2]  +1760*(int)rgbB[x*3+y*5760    +1]  -1263*(int)rgbB[x*3+y*5760    +4]    -14*(int)rgbB[x*3+y*5760    +7]
                 +2557*(int)rgbB[x*3+y*5760 +5755]  -2700*(int)rgbB[x*3+y*5760 +5758]  -3612*(int)rgbB[x*3+y*5760 +5761]  +4033*(int)rgbB[x*3+y*5760 +5764]    -96*(int)rgbB[x*3+y*5760 +5767]
                  +674*(int)rgbB[x*3+y*5760+11515]   -608*(int)rgbB[x*3+y*5760+11518]  +1743*(int)rgbB[x*3+y*5760+11521]  -1742*(int)rgbB[x*3+y*5760+11524]    +17*(int)rgbB[x*3+y*5760+11527]
               /* from frame B, blue (sum=0.08): */
                   +30*(int)rgbB[x*3+y*5760-11524]    -25*(int)rgbB[x*3+y*5760-11521]   -135*(int)rgbB[x*3+y*5760-11518]   +114*(int)rgbB[x*3+y*5760-11515]     +4*(int)rgbB[x*3+y*5760-11512]
                  +432*(int)rgbB[x*3+y*5760 -5764]   -399*(int)rgbB[x*3+y*5760 -5761]   -215*(int)rgbB[x*3+y*5760 -5758]   +223*(int)rgbB[x*3+y*5760 -5755]    +21*(int)rgbB[x*3+y*5760 -5752]
                  -465*(int)rgbB[x*3+y*5760    -4]   +548*(int)rgbB[x*3+y*5760    -1]   +370*(int)rgbB[x*3+y*5760    +2]    -82*(int)rgbB[x*3+y*5760    +5]    +11*(int)rgbB[x*3+y*5760    +8]
                  -146*(int)rgbB[x*3+y*5760 +5756]   +164*(int)rgbB[x*3+y*5760 +5759]   +683*(int)rgbB[x*3+y*5760 +5762]   -394*(int)rgbB[x*3+y*5760 +5765]    -40*(int)rgbB[x*3+y*5760 +5768]
                   +82*(int)rgbB[x*3+y*5760+11516]    -62*(int)rgbB[x*3+y*5760+11519]    +77*(int)rgbB[x*3+y*5760+11522]   -125*(int)rgbB[x*3+y*5760+11525]     -5*(int)rgbB[x*3+y*5760+11528]
            );
            int green2 = (x % 2)
              ? (  /* odd columns: */
               /* from frame A, red (sum=-0.01): */
                    -1*(int)rgbA[x*3+y*5760-11526]   +239*(int)rgbA[x*3+y*5760-11523]   -260*(int)rgbA[x*3+y*5760-11520]    -41*(int)rgbA[x*3+y*5760-11517]    +50*(int)rgbA[x*3+y*5760-11514]
                   -35*(int)rgbA[x*3+y*5760 -5766]   -338*(int)rgbA[x*3+y*5760 -5763]   +367*(int)rgbA[x*3+y*5760 -5760]     -2*(int)rgbA[x*3+y*5760 -5757]    -49*(int)rgbA[x*3+y*5760 -5754]
                    +6*(int)rgbA[x*3+y*5760    -6]    +71*(int)rgbA[x*3+y*5760    -3]     +5*(int)rgbA[x*3+y*5760    +0]   +129*(int)rgbA[x*3+y*5760    +3]   -171*(int)rgbA[x*3+y*5760    +6]
                   -13*(int)rgbA[x*3+y*5760 +5754]   -169*(int)rgbA[x*3+y*5760 +5757]   +157*(int)rgbA[x*3+y*5760 +5760]   -101*(int)rgbA[x*3+y*5760 +5763]   +126*(int)rgbA[x*3+y*5760 +5766]
                    +7*(int)rgbA[x*3+y*5760+11514]    +88*(int)rgbA[x*3+y*5760+11517]   -102*(int)rgbA[x*3+y*5760+11520]    +76*(int)rgbA[x*3+y*5760+11523]    -93*(int)rgbA[x*3+y*5760+11526]
               /* from frame A, green (sum=0.03): */
                   +26*(int)rgbA[x*3+y*5760-11525]   +155*(int)rgbA[x*3+y*5760-11522]   -186*(int)rgbA[x*3+y*5760-11519]   +351*(int)rgbA[x*3+y*5760-11516]   -334*(int)rgbA[x*3+y*5760-11513]
                   -86*(int)rgbA[x*3+y*5760 -5765]  +1027*(int)rgbA[x*3+y*5760 -5762]   -833*(int)rgbA[x*3+y*5760 -5759]   -486*(int)rgbA[x*3+y*5760 -5756]   +416*(int)rgbA[x*3+y*5760 -5753]
                   -46*(int)rgbA[x*3+y*5760    -5]   -320*(int)rgbA[x*3+y*5760    -2]   +658*(int)rgbA[x*3+y*5760    +1]   +246*(int)rgbA[x*3+y*5760    +4]   -231*(int)rgbA[x*3+y*5760    +7]
                   -27*(int)rgbA[x*3+y*5760 +5755]   +252*(int)rgbA[x*3+y*5760 +5758]   -331*(int)rgbA[x*3+y*5760 +5761]   +265*(int)rgbA[x*3+y*5760 +5764]   -271*(int)rgbA[x*3+y*5760 +5767]
                    +0*(int)rgbA[x*3+y*5760+11515]   -271*(int)rgbA[x*3+y*5760+11518]   +265*(int)rgbA[x*3+y*5760+11521]   +130*(int)rgbA[x*3+y*5760+11524]   -139*(int)rgbA[x*3+y*5760+11527]
               /* from frame A, blue (sum=-0.07): */
                    +3*(int)rgbA[x*3+y*5760-11524]   -423*(int)rgbA[x*3+y*5760-11521]   +420*(int)rgbA[x*3+y*5760-11518]   -388*(int)rgbA[x*3+y*5760-11515]   +295*(int)rgbA[x*3+y*5760-11512]
                   +13*(int)rgbA[x*3+y*5760 -5764]   -555*(int)rgbA[x*3+y*5760 -5761]   +592*(int)rgbA[x*3+y*5760 -5758]   +292*(int)rgbA[x*3+y*5760 -5755]   -410*(int)rgbA[x*3+y*5760 -5752]
                   -13*(int)rgbA[x*3+y*5760    -4]   +519*(int)rgbA[x*3+y*5760    -1]   -552*(int)rgbA[x*3+y*5760    +2]   -554*(int)rgbA[x*3+y*5760    +5]   +344*(int)rgbA[x*3+y*5760    +8]
                   -11*(int)rgbA[x*3+y*5760 +5756]   -117*(int)rgbA[x*3+y*5760 +5759]   +130*(int)rgbA[x*3+y*5760 +5762]   -277*(int)rgbA[x*3+y*5760 +5765]   +154*(int)rgbA[x*3+y*5760 +5768]
                   +10*(int)rgbA[x*3+y*5760+11516]   +153*(int)rgbA[x*3+y*5760+11519]   -150*(int)rgbA[x*3+y*5760+11522]   -299*(int)rgbA[x*3+y*5760+11525]   +223*(int)rgbA[x*3+y*5760+11528]
               /* from frame B, red (sum=0.10): */
                    -3*(int)rgbB[x*3+y*5760-11526]   +494*(int)rgbB[x*3+y*5760-11523]   -520*(int)rgbB[x*3+y*5760-11520]   -205*(int)rgbB[x*3+y*5760-11517]   +206*(int)rgbB[x*3+y*5760-11514]
                   -21*(int)rgbB[x*3+y*5760 -5766]   -130*(int)rgbB[x*3+y*5760 -5763]   +377*(int)rgbB[x*3+y*5760 -5760]   -373*(int)rgbB[x*3+y*5760 -5757]   +385*(int)rgbB[x*3+y*5760 -5754]
                   -72*(int)rgbB[x*3+y*5760    -6]  +1757*(int)rgbB[x*3+y*5760    -3]  -1148*(int)rgbB[x*3+y*5760    +0]   +332*(int)rgbB[x*3+y*5760    +3]   -361*(int)rgbB[x*3+y*5760    +6]
                   +48*(int)rgbB[x*3+y*5760 +5754]  -1194*(int)rgbB[x*3+y*5760 +5757]  +1214*(int)rgbB[x*3+y*5760 +5760]  -1636*(int)rgbB[x*3+y*5760 +5763]  +1673*(int)rgbB[x*3+y*5760 +5766]
                    -4*(int)rgbB[x*3+y*5760+11514]   -955*(int)rgbB[x*3+y*5760+11517]   +976*(int)rgbB[x*3+y*5760+11520]  +1673*(int)rgbB[x*3+y*5760+11523]  -1671*(int)rgbB[x*3+y*5760+11526]
               /* from frame B, green (sum=0.82): */
                    -4*(int)rgbB[x*3+y*5760-11525]   -725*(int)rgbB[x*3+y*5760-11522]   +805*(int)rgbB[x*3+y*5760-11519]   +334*(int)rgbB[x*3+y*5760-11516]   -321*(int)rgbB[x*3+y*5760-11513]
                   +12*(int)rgbB[x*3+y*5760 -5765]    +95*(int)rgbB[x*3+y*5760 -5762]    -17*(int)rgbB[x*3+y*5760 -5759]   +266*(int)rgbB[x*3+y*5760 -5756]   -301*(int)rgbB[x*3+y*5760 -5753]
                   +89*(int)rgbB[x*3+y*5760    -5]  -3304*(int)rgbB[x*3+y*5760    -2]  +9438*(int)rgbB[x*3+y*5760    +1]   -218*(int)rgbB[x*3+y*5760    +4]   +354*(int)rgbB[x*3+y*5760    +7]
                   -12*(int)rgbB[x*3+y*5760 +5755]  +1197*(int)rgbB[x*3+y*5760 +5758]  -1058*(int)rgbB[x*3+y*5760 +5761]  +1603*(int)rgbB[x*3+y*5760 +5764]  -1624*(int)rgbB[x*3+y*5760 +5767]
                    -6*(int)rgbB[x*3+y*5760+11515]  +1111*(int)rgbB[x*3+y*5760+11518]  -1003*(int)rgbB[x*3+y*5760+11521]  -1741*(int)rgbB[x*3+y*5760+11524]  +1736*(int)rgbB[x*3+y*5760+11527]
               /* from frame B, blue (sum=0.13): */
                   +12*(int)rgbB[x*3+y*5760-11524]   +202*(int)rgbB[x*3+y*5760-11521]   -201*(int)rgbB[x*3+y*5760-11518]   -116*(int)rgbB[x*3+y*5760-11515]   +136*(int)rgbB[x*3+y*5760-11512]
                   +12*(int)rgbB[x*3+y*5760 -5764]   -135*(int)rgbB[x*3+y*5760 -5761]    +27*(int)rgbB[x*3+y*5760 -5758]    -19*(int)rgbB[x*3+y*5760 -5755]    -56*(int)rgbB[x*3+y*5760 -5752]
                   -20*(int)rgbB[x*3+y*5760    -4]  +1950*(int)rgbB[x*3+y*5760    -1]   -576*(int)rgbB[x*3+y*5760    +2]    +16*(int)rgbB[x*3+y*5760    +5]    -20*(int)rgbB[x*3+y*5760    +8]
                    +0*(int)rgbB[x*3+y*5760 +5756]   -186*(int)rgbB[x*3+y*5760 +5759]    +97*(int)rgbB[x*3+y*5760 +5762]   -102*(int)rgbB[x*3+y*5760 +5765]    +34*(int)rgbB[x*3+y*5760 +5768]
                    +9*(int)rgbB[x*3+y*5760+11516]   -140*(int)rgbB[x*3+y*5760+11519]   +142*(int)rgbB[x*3+y*5760+11522]    +76*(int)rgbB[x*3+y*5760+11525]    -55*(int)rgbB[x*3+y*5760+11528]
            ) : (  /* even columns: */
               /* from frame A, red (sum=-0.21): */
                  -192*(int)rgbA[x*3+y*5760-11526]   +269*(int)rgbA[x*3+y*5760-11523]   -126*(int)rgbA[x*3+y*5760-11520]   -106*(int)rgbA[x*3+y*5760-11517]    +12*(int)rgbA[x*3+y*5760-11514]
                  +338*(int)rgbA[x*3+y*5760 -5766]   -376*(int)rgbA[x*3+y*5760 -5763]   +172*(int)rgbA[x*3+y*5760 -5760]    -49*(int)rgbA[x*3+y*5760 -5757]    +20*(int)rgbA[x*3+y*5760 -5754]
                  -158*(int)rgbA[x*3+y*5760    -6]    +45*(int)rgbA[x*3+y*5760    -3]   -313*(int)rgbA[x*3+y*5760    +0]   -824*(int)rgbA[x*3+y*5760    +3]    -89*(int)rgbA[x*3+y*5760    +6]
                  +219*(int)rgbA[x*3+y*5760 +5754]   -143*(int)rgbA[x*3+y*5760 +5757]   +258*(int)rgbA[x*3+y*5760 +5760]   -549*(int)rgbA[x*3+y*5760 +5763]     -1*(int)rgbA[x*3+y*5760 +5766]
                  -165*(int)rgbA[x*3+y*5760+11514]   +201*(int)rgbA[x*3+y*5760+11517]    -24*(int)rgbA[x*3+y*5760+11520]   -150*(int)rgbA[x*3+y*5760+11523]    +14*(int)rgbA[x*3+y*5760+11526]
               /* from frame A, green (sum=0.04): */
                  +365*(int)rgbA[x*3+y*5760-11525]   -369*(int)rgbA[x*3+y*5760-11522]   -161*(int)rgbA[x*3+y*5760-11519]   +124*(int)rgbA[x*3+y*5760-11516]    -13*(int)rgbA[x*3+y*5760-11513]
                  -811*(int)rgbA[x*3+y*5760 -5765]   +921*(int)rgbA[x*3+y*5760 -5762]  +1108*(int)rgbA[x*3+y*5760 -5759]   -881*(int)rgbA[x*3+y*5760 -5756]     -8*(int)rgbA[x*3+y*5760 -5753]
                  +422*(int)rgbA[x*3+y*5760    -5]   -414*(int)rgbA[x*3+y*5760    -2]    +96*(int)rgbA[x*3+y*5760    +1]    -54*(int)rgbA[x*3+y*5760    +4]    -45*(int)rgbA[x*3+y*5760    +7]
                  -118*(int)rgbA[x*3+y*5760 +5755]   +145*(int)rgbA[x*3+y*5760 +5758]   -346*(int)rgbA[x*3+y*5760 +5761]   +398*(int)rgbA[x*3+y*5760 +5764]    +32*(int)rgbA[x*3+y*5760 +5767]
                  +127*(int)rgbA[x*3+y*5760+11515]   -153*(int)rgbA[x*3+y*5760+11518]   +359*(int)rgbA[x*3+y*5760+11521]   -350*(int)rgbA[x*3+y*5760+11524]    -25*(int)rgbA[x*3+y*5760+11527]
               /* from frame A, blue (sum=0.03): */
                  -134*(int)rgbA[x*3+y*5760-11524]   +106*(int)rgbA[x*3+y*5760-11521]   +237*(int)rgbA[x*3+y*5760-11518]   -250*(int)rgbA[x*3+y*5760-11515]     -7*(int)rgbA[x*3+y*5760-11512]
                  +377*(int)rgbA[x*3+y*5760 -5764]   -354*(int)rgbA[x*3+y*5760 -5761]   -860*(int)rgbA[x*3+y*5760 -5758]   +792*(int)rgbA[x*3+y*5760 -5755]     +6*(int)rgbA[x*3+y*5760 -5752]
                  -283*(int)rgbA[x*3+y*5760    -4]   +373*(int)rgbA[x*3+y*5760    -1]   +421*(int)rgbA[x*3+y*5760    +2]   -147*(int)rgbA[x*3+y*5760    +5]    +53*(int)rgbA[x*3+y*5760    +8]
                   -72*(int)rgbA[x*3+y*5760 +5756]    +46*(int)rgbA[x*3+y*5760 +5759]   +105*(int)rgbA[x*3+y*5760 +5762]   -116*(int)rgbA[x*3+y*5760 +5765]     +2*(int)rgbA[x*3+y*5760 +5768]
                    +3*(int)rgbA[x*3+y*5760+11516]    -15*(int)rgbA[x*3+y*5760+11519]   -294*(int)rgbA[x*3+y*5760+11522]   +293*(int)rgbA[x*3+y*5760+11525]    -17*(int)rgbA[x*3+y*5760+11528]
               /* from frame B, red (sum=0.31): */
                  +176*(int)rgbB[x*3+y*5760-11526]   -180*(int)rgbB[x*3+y*5760-11523]  -1212*(int)rgbB[x*3+y*5760-11520]  +1263*(int)rgbB[x*3+y*5760-11517]    +28*(int)rgbB[x*3+y*5760-11514]
                  +889*(int)rgbB[x*3+y*5760 -5766]  -1131*(int)rgbB[x*3+y*5760 -5763]  +2059*(int)rgbB[x*3+y*5760 -5760]  -2082*(int)rgbB[x*3+y*5760 -5757]    -10*(int)rgbB[x*3+y*5760 -5754]
                  -619*(int)rgbB[x*3+y*5760    -6]   +553*(int)rgbB[x*3+y*5760    -3]  -4871*(int)rgbB[x*3+y*5760    +0]  +7696*(int)rgbB[x*3+y*5760    +3]    -71*(int)rgbB[x*3+y*5760    +6]
                  -157*(int)rgbB[x*3+y*5760 +5754]    +57*(int)rgbB[x*3+y*5760 +5757]  +5117*(int)rgbB[x*3+y*5760 +5760]  -5207*(int)rgbB[x*3+y*5760 +5763]    +39*(int)rgbB[x*3+y*5760 +5766]
                  -195*(int)rgbB[x*3+y*5760+11514]   +216*(int)rgbB[x*3+y*5760+11517]  -1940*(int)rgbB[x*3+y*5760+11520]  +2020*(int)rgbB[x*3+y*5760+11523]    +64*(int)rgbB[x*3+y*5760+11526]
               /* from frame B, green (sum=0.80): */
                  -221*(int)rgbB[x*3+y*5760-11525]   +234*(int)rgbB[x*3+y*5760-11522]  +1272*(int)rgbB[x*3+y*5760-11519]  -1196*(int)rgbB[x*3+y*5760-11516]     +9*(int)rgbB[x*3+y*5760-11513]
                  -848*(int)rgbB[x*3+y*5760 -5765]   +777*(int)rgbB[x*3+y*5760 -5762]  -1809*(int)rgbB[x*3+y*5760 -5759]  +1913*(int)rgbB[x*3+y*5760 -5756]    -23*(int)rgbB[x*3+y*5760 -5753]
                  +801*(int)rgbB[x*3+y*5760    -5]   -487*(int)rgbB[x*3+y*5760    -2] +13730*(int)rgbB[x*3+y*5760    +1]  -7747*(int)rgbB[x*3+y*5760    +4]   +120*(int)rgbB[x*3+y*5760    +7]
                  +184*(int)rgbB[x*3+y*5760 +5755]   -265*(int)rgbB[x*3+y*5760 +5758]  -5164*(int)rgbB[x*3+y*5760 +5761]  +5157*(int)rgbB[x*3+y*5760 +5764]    +18*(int)rgbB[x*3+y*5760 +5767]
                   +65*(int)rgbB[x*3+y*5760+11515]    -41*(int)rgbB[x*3+y*5760+11518]  +2065*(int)rgbB[x*3+y*5760+11521]  -1929*(int)rgbB[x*3+y*5760+11524]    -22*(int)rgbB[x*3+y*5760+11527]
               /* from frame B, blue (sum=0.03): */
                   +32*(int)rgbB[x*3+y*5760-11524]    -62*(int)rgbB[x*3+y*5760-11521]    +67*(int)rgbB[x*3+y*5760-11518]    -60*(int)rgbB[x*3+y*5760-11515]     +7*(int)rgbB[x*3+y*5760-11512]
                   -95*(int)rgbB[x*3+y*5760 -5764]    +91*(int)rgbB[x*3+y*5760 -5761]   +151*(int)rgbB[x*3+y*5760 -5758]    -40*(int)rgbB[x*3+y*5760 -5755]    +15*(int)rgbB[x*3+y*5760 -5752]
                  -106*(int)rgbB[x*3+y*5760    -4]   +113*(int)rgbB[x*3+y*5760    -1]   -382*(int)rgbB[x*3+y*5760    +2]   +384*(int)rgbB[x*3+y*5760    +5]     -9*(int)rgbB[x*3+y*5760    +8]
                   -14*(int)rgbB[x*3+y*5760 +5756]    +21*(int)rgbB[x*3+y*5760 +5759]   +231*(int)rgbB[x*3+y*5760 +5762]   -147*(int)rgbB[x*3+y*5760 +5765]    +31*(int)rgbB[x*3+y*5760 +5768]
                   +85*(int)rgbB[x*3+y*5760+11516]   -111*(int)rgbB[x*3+y*5760+11519]    +88*(int)rgbB[x*3+y*5760+11522]    -53*(int)rgbB[x*3+y*5760+11525]    -17*(int)rgbB[x*3+y*5760+11528]
            );
            int blue = (x % 2)
              ? (  /* odd columns: */
               /* from frame A, red (sum=-0.04): */
                   +26*(int)rgbA[x*3+y*5760-11526]   -134*(int)rgbA[x*3+y*5760-11523]   +176*(int)rgbA[x*3+y*5760-11520]   +139*(int)rgbA[x*3+y*5760-11517]   -217*(int)rgbA[x*3+y*5760-11514]
                   -14*(int)rgbA[x*3+y*5760 -5766]   +447*(int)rgbA[x*3+y*5760 -5763]   -480*(int)rgbA[x*3+y*5760 -5760]   +350*(int)rgbA[x*3+y*5760 -5757]   -347*(int)rgbA[x*3+y*5760 -5754]
                    +9*(int)rgbA[x*3+y*5760    -6]   +296*(int)rgbA[x*3+y*5760    -3]   -305*(int)rgbA[x*3+y*5760    +0]   -416*(int)rgbA[x*3+y*5760    +3]   +178*(int)rgbA[x*3+y*5760    +6]
                    +5*(int)rgbA[x*3+y*5760 +5754]   -474*(int)rgbA[x*3+y*5760 +5757]   +485*(int)rgbA[x*3+y*5760 +5760]    -99*(int)rgbA[x*3+y*5760 +5763]    +47*(int)rgbA[x*3+y*5760 +5766]
                   -29*(int)rgbA[x*3+y*5760+11514]   +298*(int)rgbA[x*3+y*5760+11517]   -283*(int)rgbA[x*3+y*5760+11520]    +87*(int)rgbA[x*3+y*5760+11523]    -70*(int)rgbA[x*3+y*5760+11526]
               /* from frame A, green (sum=-0.06): */
                   +50*(int)rgbA[x*3+y*5760-11525]   +580*(int)rgbA[x*3+y*5760-11522]   -712*(int)rgbA[x*3+y*5760-11519]   -732*(int)rgbA[x*3+y*5760-11516]   +713*(int)rgbA[x*3+y*5760-11513]
                   -10*(int)rgbA[x*3+y*5760 -5765]  -1099*(int)rgbA[x*3+y*5760 -5762]  +1331*(int)rgbA[x*3+y*5760 -5759]  -2187*(int)rgbA[x*3+y*5760 -5756]  +2189*(int)rgbA[x*3+y*5760 -5753]
                   -48*(int)rgbA[x*3+y*5760    -5]  -1085*(int)rgbA[x*3+y*5760    -2]  +1444*(int)rgbA[x*3+y*5760    +1]    +30*(int)rgbA[x*3+y*5760    +4]   -753*(int)rgbA[x*3+y*5760    +7]
                   +53*(int)rgbA[x*3+y*5760 +5755]   +462*(int)rgbA[x*3+y*5760 +5758]   -644*(int)rgbA[x*3+y*5760 +5761]    +46*(int)rgbA[x*3+y*5760 +5764]    -55*(int)rgbA[x*3+y*5760 +5767]
                   +12*(int)rgbA[x*3+y*5760+11515]   -279*(int)rgbA[x*3+y*5760+11518]   +190*(int)rgbA[x*3+y*5760+11521]   -971*(int)rgbA[x*3+y*5760+11524]   +983*(int)rgbA[x*3+y*5760+11527]
               /* from frame A, blue (sum=0.84): */
                   -27*(int)rgbA[x*3+y*5760-11524]   -548*(int)rgbA[x*3+y*5760-11521]   +464*(int)rgbA[x*3+y*5760-11518]   +895*(int)rgbA[x*3+y*5760-11515]   -535*(int)rgbA[x*3+y*5760-11512]
                   -22*(int)rgbA[x*3+y*5760 -5764]   +437*(int)rgbA[x*3+y*5760 -5761]   -481*(int)rgbA[x*3+y*5760 -5758]  +2574*(int)rgbA[x*3+y*5760 -5755]  -1888*(int)rgbA[x*3+y*5760 -5752]
                   +50*(int)rgbA[x*3+y*5760    -4]   +937*(int)rgbA[x*3+y*5760    -1]   -666*(int)rgbA[x*3+y*5760    +2]  +4184*(int)rgbA[x*3+y*5760    +5]   +715*(int)rgbA[x*3+y*5760    +8]
                   +22*(int)rgbA[x*3+y*5760 +5756]   -134*(int)rgbA[x*3+y*5760 +5759]    +14*(int)rgbA[x*3+y*5760 +5762]   +794*(int)rgbA[x*3+y*5760 +5765]   -113*(int)rgbA[x*3+y*5760 +5768]
                   -18*(int)rgbA[x*3+y*5760+11516]   -132*(int)rgbA[x*3+y*5760+11519]    +60*(int)rgbA[x*3+y*5760+11522]  +1232*(int)rgbA[x*3+y*5760+11525]   -905*(int)rgbA[x*3+y*5760+11528]
               /* from frame B, red (sum=0.03): */
                   +64*(int)rgbB[x*3+y*5760-11526]  +2408*(int)rgbB[x*3+y*5760-11523]  -2446*(int)rgbB[x*3+y*5760-11520]    +55*(int)rgbB[x*3+y*5760-11517]    -75*(int)rgbB[x*3+y*5760-11514]
                   -33*(int)rgbB[x*3+y*5760 -5766]  -6172*(int)rgbB[x*3+y*5760 -5763]  +6255*(int)rgbB[x*3+y*5760 -5760]  +1046*(int)rgbB[x*3+y*5760 -5757]  -1187*(int)rgbB[x*3+y*5760 -5754]
                    -7*(int)rgbB[x*3+y*5760    -6]  +5487*(int)rgbB[x*3+y*5760    -3]  -5375*(int)rgbB[x*3+y*5760    +0]   -786*(int)rgbB[x*3+y*5760    +3]  +1058*(int)rgbB[x*3+y*5760    +6]
                   +39*(int)rgbB[x*3+y*5760 +5754]   +347*(int)rgbB[x*3+y*5760 +5757]   -457*(int)rgbB[x*3+y*5760 +5760]  -1434*(int)rgbB[x*3+y*5760 +5763]  +1383*(int)rgbB[x*3+y*5760 +5766]
                   +50*(int)rgbB[x*3+y*5760+11514]   +383*(int)rgbB[x*3+y*5760+11517]   -412*(int)rgbB[x*3+y*5760+11520]  -1554*(int)rgbB[x*3+y*5760+11523]  +1577*(int)rgbB[x*3+y*5760+11526]
               /* from frame B, green (sum=0.03): */
                   +32*(int)rgbB[x*3+y*5760-11525]  -2287*(int)rgbB[x*3+y*5760-11522]  +2177*(int)rgbB[x*3+y*5760-11519]   -318*(int)rgbB[x*3+y*5760-11516]   +263*(int)rgbB[x*3+y*5760-11513]
                    +3*(int)rgbB[x*3+y*5760 -5765]  +5839*(int)rgbB[x*3+y*5760 -5762]  -6113*(int)rgbB[x*3+y*5760 -5759]  -1466*(int)rgbB[x*3+y*5760 -5756]  +1314*(int)rgbB[x*3+y*5760 -5753]
                   -76*(int)rgbB[x*3+y*5760    -5]  -5389*(int)rgbB[x*3+y*5760    -2]  +6063*(int)rgbB[x*3+y*5760    +1]  +1695*(int)rgbB[x*3+y*5760    +4]  -1164*(int)rgbB[x*3+y*5760    +7]
                   +20*(int)rgbB[x*3+y*5760 +5755]   -343*(int)rgbB[x*3+y*5760 +5758]    +68*(int)rgbB[x*3+y*5760 +5761]  +1316*(int)rgbB[x*3+y*5760 +5764]  -1357*(int)rgbB[x*3+y*5760 +5767]
                   +40*(int)rgbB[x*3+y*5760+11515]   -439*(int)rgbB[x*3+y*5760+11518]   +385*(int)rgbB[x*3+y*5760+11521]  +1557*(int)rgbB[x*3+y*5760+11524]  -1557*(int)rgbB[x*3+y*5760+11527]
               /* from frame B, blue (sum=0.20): */
                   -49*(int)rgbB[x*3+y*5760-11524]   -110*(int)rgbB[x*3+y*5760-11521]    +85*(int)rgbB[x*3+y*5760-11518]   +206*(int)rgbB[x*3+y*5760-11515]   -231*(int)rgbB[x*3+y*5760-11512]
                   +22*(int)rgbB[x*3+y*5760 -5764]   +369*(int)rgbB[x*3+y*5760 -5761]   -293*(int)rgbB[x*3+y*5760 -5758]   +400*(int)rgbB[x*3+y*5760 -5755]   -248*(int)rgbB[x*3+y*5760 -5752]
                   +78*(int)rgbB[x*3+y*5760    -4]   +170*(int)rgbB[x*3+y*5760    -1]   +471*(int)rgbB[x*3+y*5760    +2]   +191*(int)rgbB[x*3+y*5760    +5]   +331*(int)rgbB[x*3+y*5760    +8]
                   -18*(int)rgbB[x*3+y*5760 +5756]    +38*(int)rgbB[x*3+y*5760 +5759]   +104*(int)rgbB[x*3+y*5760 +5762]   +201*(int)rgbB[x*3+y*5760 +5765]     +6*(int)rgbB[x*3+y*5760 +5768]
                   -48*(int)rgbB[x*3+y*5760+11516]   +163*(int)rgbB[x*3+y*5760+11519]   -145*(int)rgbB[x*3+y*5760+11522]    -48*(int)rgbB[x*3+y*5760+11525]     -5*(int)rgbB[x*3+y*5760+11528]
            ) : (  /* even columns: */
               /* from frame A, red (sum=0.04): */
                   -24*(int)rgbA[x*3+y*5760-11526]   +107*(int)rgbA[x*3+y*5760-11523]   +153*(int)rgbA[x*3+y*5760-11520]   -301*(int)rgbA[x*3+y*5760-11517]    +28*(int)rgbA[x*3+y*5760-11514]
                  +180*(int)rgbA[x*3+y*5760 -5766]   -282*(int)rgbA[x*3+y*5760 -5763]   -368*(int)rgbA[x*3+y*5760 -5760]   +762*(int)rgbA[x*3+y*5760 -5757]    +12*(int)rgbA[x*3+y*5760 -5754]
                   -75*(int)rgbA[x*3+y*5760    -6]     +7*(int)rgbA[x*3+y*5760    -3]   -781*(int)rgbA[x*3+y*5760    +0]  +1095*(int)rgbA[x*3+y*5760    +3]    -71*(int)rgbA[x*3+y*5760    +6]
                  +140*(int)rgbA[x*3+y*5760 +5754]    -54*(int)rgbA[x*3+y*5760 +5757]   +585*(int)rgbA[x*3+y*5760 +5760]   -709*(int)rgbA[x*3+y*5760 +5763]     +4*(int)rgbA[x*3+y*5760 +5766]
                  -102*(int)rgbA[x*3+y*5760+11514]   +103*(int)rgbA[x*3+y*5760+11517]    -45*(int)rgbA[x*3+y*5760+11520]    -25*(int)rgbA[x*3+y*5760+11523]    +24*(int)rgbA[x*3+y*5760+11526]
               /* from frame A, green (sum=0.01): */
                    -8*(int)rgbA[x*3+y*5760-11525]    +20*(int)rgbA[x*3+y*5760-11522]   -771*(int)rgbA[x*3+y*5760-11519]   +623*(int)rgbA[x*3+y*5760-11516]    +39*(int)rgbA[x*3+y*5760-11513]
                  -883*(int)rgbA[x*3+y*5760 -5765]   +856*(int)rgbA[x*3+y*5760 -5762]  +3152*(int)rgbA[x*3+y*5760 -5759]  -2890*(int)rgbA[x*3+y*5760 -5756]     +4*(int)rgbA[x*3+y*5760 -5753]
                  +935*(int)rgbA[x*3+y*5760    -5]  -1035*(int)rgbA[x*3+y*5760    -2]   +621*(int)rgbA[x*3+y*5760    +1]   -336*(int)rgbA[x*3+y*5760    +4]   -119*(int)rgbA[x*3+y*5760    +7]
                  -564*(int)rgbA[x*3+y*5760 +5755]   +588*(int)rgbA[x*3+y*5760 +5758]   -155*(int)rgbA[x*3+y*5760 +5761]     +8*(int)rgbA[x*3+y*5760 +5764]    +75*(int)rgbA[x*3+y*5760 +5767]
                   +24*(int)rgbA[x*3+y*5760+11515]    -52*(int)rgbA[x*3+y*5760+11518]   +561*(int)rgbA[x*3+y*5760+11521]   -592*(int)rgbA[x*3+y*5760+11524]    -10*(int)rgbA[x*3+y*5760+11527]
               /* from frame A, blue (sum=0.16): */
                  +107*(int)rgbA[x*3+y*5760-11524]   -177*(int)rgbA[x*3+y*5760-11521]   +510*(int)rgbA[x*3+y*5760-11518]   -491*(int)rgbA[x*3+y*5760-11515]    +16*(int)rgbA[x*3+y*5760-11512]
                  +591*(int)rgbA[x*3+y*5760 -5764]   -579*(int)rgbA[x*3+y*5760 -5761]  -2113*(int)rgbA[x*3+y*5760 -5758]  +2143*(int)rgbA[x*3+y*5760 -5755]   +149*(int)rgbA[x*3+y*5760 -5752]
                  -893*(int)rgbA[x*3+y*5760    -4]  +1024*(int)rgbA[x*3+y*5760    -1]   +959*(int)rgbA[x*3+y*5760    +2]   -498*(int)rgbA[x*3+y*5760    +5]   +387*(int)rgbA[x*3+y*5760    +8]
                  +495*(int)rgbA[x*3+y*5760 +5756]   -531*(int)rgbA[x*3+y*5760 +5759]   -441*(int)rgbA[x*3+y*5760 +5762]   +615*(int)rgbA[x*3+y*5760 +5765]    +62*(int)rgbA[x*3+y*5760 +5768]
                   +52*(int)rgbA[x*3+y*5760+11516]   -102*(int)rgbA[x*3+y*5760+11519]   -513*(int)rgbA[x*3+y*5760+11522]   +539*(int)rgbA[x*3+y*5760+11525]    +16*(int)rgbA[x*3+y*5760+11528]
               /* from frame B, red (sum=-0.06): */
                  -335*(int)rgbB[x*3+y*5760-11526]   +399*(int)rgbB[x*3+y*5760-11523]   +328*(int)rgbB[x*3+y*5760-11520]   -431*(int)rgbB[x*3+y*5760-11517]    +37*(int)rgbB[x*3+y*5760-11514]
                  -241*(int)rgbB[x*3+y*5760 -5766]    +13*(int)rgbB[x*3+y*5760 -5763]  +4719*(int)rgbB[x*3+y*5760 -5760]  -4744*(int)rgbB[x*3+y*5760 -5757]    -43*(int)rgbB[x*3+y*5760 -5754]
                  -530*(int)rgbB[x*3+y*5760    -6]   +404*(int)rgbB[x*3+y*5760    -3] -10083*(int)rgbB[x*3+y*5760    +0] +10100*(int)rgbB[x*3+y*5760    +3]    -55*(int)rgbB[x*3+y*5760    +6]
                  -374*(int)rgbB[x*3+y*5760 +5754]   +367*(int)rgbB[x*3+y*5760 +5757]  +5216*(int)rgbB[x*3+y*5760 +5760]  -5344*(int)rgbB[x*3+y*5760 +5763]    +14*(int)rgbB[x*3+y*5760 +5766]
                  -903*(int)rgbB[x*3+y*5760+11514]   +966*(int)rgbB[x*3+y*5760+11517]   +357*(int)rgbB[x*3+y*5760+11520]   -369*(int)rgbB[x*3+y*5760+11523]    +45*(int)rgbB[x*3+y*5760+11526]
               /* from frame B, green (sum=-0.04): */
                  +156*(int)rgbB[x*3+y*5760-11525]   -123*(int)rgbB[x*3+y*5760-11522]   +102*(int)rgbB[x*3+y*5760-11519]   -229*(int)rgbB[x*3+y*5760-11516]     +7*(int)rgbB[x*3+y*5760-11513]
                  +383*(int)rgbB[x*3+y*5760 -5765]   -443*(int)rgbB[x*3+y*5760 -5762]  -4815*(int)rgbB[x*3+y*5760 -5759]  +4578*(int)rgbB[x*3+y*5760 -5756]    -30*(int)rgbB[x*3+y*5760 -5753]
                  +929*(int)rgbB[x*3+y*5760    -5]   -858*(int)rgbB[x*3+y*5760    -2]  +9030*(int)rgbB[x*3+y*5760    +1]  -8584*(int)rgbB[x*3+y*5760    +4]    -27*(int)rgbB[x*3+y*5760    +7]
                  +463*(int)rgbB[x*3+y*5760 +5755]   -566*(int)rgbB[x*3+y*5760 +5758]  -5256*(int)rgbB[x*3+y*5760 +5761]  +4995*(int)rgbB[x*3+y*5760 +5764]    -11*(int)rgbB[x*3+y*5760 +5767]
                  +538*(int)rgbB[x*3+y*5760+11515]   -481*(int)rgbB[x*3+y*5760+11518]   -156*(int)rgbB[x*3+y*5760+11521]    +61*(int)rgbB[x*3+y*5760+11524]    +17*(int)rgbB[x*3+y*5760+11527]
               /* from frame B, blue (sum=0.88): */
                  +178*(int)rgbB[x*3+y*5760-11524]   -260*(int)rgbB[x*3+y*5760-11521]   -242*(int)rgbB[x*3+y*5760-11518]   +545*(int)rgbB[x*3+y*5760-11515]    -58*(int)rgbB[x*3+y*5760-11512]
                  -286*(int)rgbB[x*3+y*5760 -5764]   +205*(int)rgbB[x*3+y*5760 -5761]   +667*(int)rgbB[x*3+y*5760 -5758]    -39*(int)rgbB[x*3+y*5760 -5755]    -72*(int)rgbB[x*3+y*5760 -5752]
                  -170*(int)rgbB[x*3+y*5760    -4]   +393*(int)rgbB[x*3+y*5760    -1]  +6351*(int)rgbB[x*3+y*5760    +2]   -829*(int)rgbB[x*3+y*5760    +5]   +231*(int)rgbB[x*3+y*5760    +8]
                  -192*(int)rgbB[x*3+y*5760 +5756]    +90*(int)rgbB[x*3+y*5760 +5759]   +610*(int)rgbB[x*3+y*5760 +5762]    +29*(int)rgbB[x*3+y*5760 +5765]   -100*(int)rgbB[x*3+y*5760 +5768]
                  +349*(int)rgbB[x*3+y*5760+11516]   -411*(int)rgbB[x*3+y*5760+11519]    +25*(int)rgbB[x*3+y*5760+11522]   +276*(int)rgbB[x*3+y*5760+11525]    -61*(int)rgbB[x*3+y*5760+11528]
            );
            
            raw[2*x+0 + (2*y+1) * (2*width)] = COERCE(red    / 8192, 0, 65535);
            raw[2*x+1 + (2*y+1) * (2*width)] = COERCE(green1 / 8192, 0, 65535);
            raw[2*x+0 + (2*y+0) * (2*width)] = COERCE(green2 / 8192, 0, 65535);
            raw[2*x+1 + (2*y+0) * (2*width)] = COERCE(blue   / 8192, 0, 65535);
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
        recover_bayer_data_5x5_brute_force(raw, rgbA, rgbB);
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

        double t0,t1,t2;
        t0 = omp_get_wtime();

        fprintf(stderr, "Recovering raw data...\n");
        recover_raw_data(raw, rgbA, rgbB);

        t1 = omp_get_wtime() - t0;
        
        fprintf(stderr, "Convert to linear...\n");
        convert_to_linear(raw, 2*width, 2*height);
        
        if (dark)
        {
            fprintf(stderr, "Darkframe subtract...\n");
            darkframe_subtract(raw, dark, 2*width, 2*height);
        }

        t2 = omp_get_wtime() - t0;
        fprintf(stderr, "Processing took %.2fs (%.2fs filtering, %.2fs others)\n", t2, t1, t2 - t1);

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
