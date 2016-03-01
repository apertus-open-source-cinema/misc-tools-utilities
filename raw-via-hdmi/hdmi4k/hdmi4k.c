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
//#include "wirth.h"

/* image data */
uint16_t* rgb;
int width;
int height;

/* dark frame */
uint16_t* dark = 0;

/* options */
int fixpn = 0;
int fixpn_flags1;
int fixpn_flags2;
float exposure = 0;
int filter = 0;
int out_4k = 1;
int use_darkframe = 0;
int use_lut = 0;
int use_matrix = 0;
float in_gamma = 0.5;
float out_gamma = 1;
int color_smooth_passes = 0;

struct cmd_group options[] = {
    {
        "Processing options", (struct cmd_option[]) {
            { &fixpn,          1,  "--fixpn",        "Fix row and column noise (SLOW, guesswork)" },
            { (void*)&exposure,1,  "--exposure=%f",  "Exposure compensation (EV)" },
            { &out_4k,         0,  "--1080p",        "1080p output (disable 4k)" },
            { &filter,         1,  "--filter=%d",    "Use a RGB filter (valid values: 1). 1080p only." },
            { (void*)&in_gamma, 1, "--in-gamma=%f",  "Gamma value used when recording (as configured in camera)" },
            { (void*)&out_gamma,1, "--out-gamma=%f", "Gamma correction for output (just for tests)" },
            { &color_smooth_passes, 3, "--cs",       "Apply 3 passes of color smoothing (from ufraw)" },
            { &color_smooth_passes, 1, "--cs=%d",    "Apply N passes of color smoothing (from ufraw)" },
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

static uint16_t Lut_R[65536];
static uint16_t Lut_G[65536];
static uint16_t Lut_B[65536];

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

static void convert_to_linear_and_subtract_darkframe(uint16_t * rgb, uint16_t * darkframe, int offset)
{
    /* test footage was recorded with gamma 0.5 */
    /* darkframe frame median is about 10000 */
    /* clipping point is about 50000 */
    double gain = (65535 - offset) / 40000.0 * powf(2, exposure);

    for (int i = 0; i < width*height*3; i++)
    {
        double data = rgb[i] / 65535.0;
        double dark = darkframe ? darkframe[i] / 65535.0 : 0;
        if (in_gamma == 0.5)
        {
            data *= data;
            dark *= dark;
        }
        else
        {
            data = pow(data, 1/in_gamma);
            dark = pow(dark, 1/in_gamma);
        }
        rgb[i] = COERCE((data - dark) * 65535 * gain + offset, 0, 65535);
    }
}

/**
 * The sum of these filters is 0 when filtering
 * different channels, and is 1 on the same channel.
 * 
 * Reason: this way, the filters will not mix the color channels,
 * but they can borrow local details (sharpness) from other channels.
 */

const int filters_1[3][2][3][3] = {
    /* Red: */
    {
        /* from red: */
        {
            {   780,  972,  641 },
            {  1036, 2216,  975 },
            {   541,  425,  606 },
        },
        /* from green: */
        {
            {  -696,  239,   65 },
            {  -729, 2419,   95 },
            {  -832,  428, -989 },
        },
    },
    /* Green: */
    {
        /* from green: */
        {
            {   669,  482,  736 },
            {  1172, 2511,  979 },
            {   684,  759,  200 },
        },
        /* from red and blue: */
        {
            {  -914,  451, -699 },
            {  -405, 2523, -155 },
            {  -629,  326, -498 },
        },
    },
    /* Blue: */
    {
        /* from blue: */
        {
            {   391,  468,  623 },
            {   716, 1694, 1078 },
            {   752, 1220, 1250 },
        },
        /* from green: */
        {
            {  -139,  109, -607 },
            {   335, 1498, -630 },
            {   305,   14, -885 },
        },
    },
};

const int filters_4k[4][3][2][3][3] = {
    /* Sub-image #1 (0,0): */
    {
        /* Red: */
        {
            /* from red: */
            {
                {  1345, 1558,  397 },
                {  1136, 1918,  436 },
                {   630,  184,  588 },
            },
            /* from green: */
            {
                {  -923,  949,   -6 },
                {  -221, 2856, -516 },
                {  -864, -424, -851 },
            },
        },
        /* Green: */
        {
            /* from green: */
            {
                {   734,  973,  761 },
                {  1593, 2623,  522 },
                {   591,  137,  258 },
            },
            /* from red and blue: */
            {
                {  -610, 1155,-1055 },
                {   -50, 2497, -709 },
                {  -520, -256, -452 },
            },
        },
        /* Blue: */
        {
            /* from blue: */
            {
                {   531,  568,  601 },
                {  1177, 2050, 1019 },
                {   587,  783,  876 },
            },
            /* from green: */
            {
                {   -43,  614, -783 },
                {   750, 1312,-1126 },
                {   299, -464, -559 },
            },
        },
    },
    /* Sub-image #2 (0,1): */
    {
        /* Red: */
        {
            /* from red: */
            {
                {   755, 1479, 1113 },
                {   415, 1823, 1242 },
                {   517,  190,  658 },
            },
            /* from green: */
            {
                {  -848,  521,  307 },
                {  -932, 2385,  620 },
                {  -646, -283,-1124 },
            },
        },
        /* Green: */
        {
            /* from green: */
            {
                {   681,  878,  907 },
                {   900, 2535, 1351 },
                {   625,  162,  153 },
            },
            /* from red and blue: */
            {
                { -1068, 1002, -447 },
                { -1091, 2339,  436 },
                {  -470, -179, -522 },
            },
        },
        /* Blue: */
        {
            /* from blue: */
            {
                {   330,  654,  711 },
                {   530, 1962, 1762 },
                {   452,  755, 1036 },
            },
            /* from green: */
            {
                {  -164,  446, -521 },
                {    34, 1501, -551 },
                {   274, -251, -768 },
            },
        },
    },
    /* Sub-image #3 (1,0): */
    {
        /* Red: */
        {
            /* from red: */
            {
                {   568,  229,  403 },
                {  1825, 2789,  464 },
                {   755,  594,  565 },
            },
            /* from green: */
            {
                {  -398, -244,  202 },
                {  -885, 2751, -592 },
                {  -821,  945, -958 },
            },
        },
        /* Green: */
        {
            /* from green: */
            {
                {   760,  -79,  860 },
                {  1220, 2625,  453 },
                {   954, 1193,  206 },
            },
            /* from red and blue: */
            {
                { -1018, -300, -624 },
                {   303, 2928, -979 },
                {  -512,  742, -540 },
            },
        },
        /* Blue: */
        {
            /* from blue: */
            {
                {   411,  332,  684 },
                {   734, 1397,  463 },
                {  1169, 1654, 1348 },
            },
            /* from green: */
            {
                {  -142, -468, -475 },
                {   549, 1810, -952 },
                {   567,  109, -998 },
            },
        },
    },
    /* Sub-image #4 (1,1): */
    {
        /* Red: */
        {
            /* from red: */
            {
                {   469,  193,  543 },
                {   807, 2594, 1685 },
                {   487,  655,  759 },
            },
            /* from green: */
            {
                {  -193, -316,   85 },
                { -1328, 2020,  514 },
                { -1032,  969, -719 },
            },
        },
        /* Green: */
        {
            /* from green: */
            {
                {   816,   41,  658 },
                {   669, 2370, 1279 },
                {   695, 1231,  433 },
            },
            /* from red and blue: */
            {
                {  -881, -438, -610 },
                {  -853, 2679,  428 },
                {  -897,  842, -270 },
            },
        },
        /* Blue: */
        {
            /* from blue: */
            {
                {   340,  397,  723 },
                {   250, 1411,  961 },
                {   718, 1547, 1845 },
            },
            /* from green: */
            {
                {    28, -497, -682 },
                {  -100, 1644,  -84 },
                {   211,  539,-1059 },
            },
        },
    },
};

static void rgb_filter_1(uint16_t* rgb, const int filters[3][2][3][3])
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
                for (int ip = 0; ip < (c == 1 ? 3 : 2); ip++)
                {
                    int p = (ip == 0) ? c :                 /* first predictor: same channel (this filter has nonzero mean) */
                            (c  == 1) ? (ip == 1 ? 0 : 2)   /* green has 3 predictors, last two being red and blue, and these filters have zero mean */
                                      : 1 ;                 /* red/blue have 2 predictors, second one is green, and its filter has zero mean */
                    
                    int f = MIN(ip,1);                      /* the two extra predictors from green use the same filter */
                    
                    aux[x*3+c + y*width*3] +=
                        filters[c][f][0][0] * rgb[(x-1)*3+p + (y-1)*width*3] + filters[c][f][0][1] * rgb[x*3+p + (y-1)*width*3] + filters[c][f][0][2] * rgb[(x+1)*3+p + (y-1)*width*3] +
                        filters[c][f][1][0] * rgb[(x-1)*3+p + (y+0)*width*3] + filters[c][f][1][1] * rgb[x*3+p + (y+0)*width*3] + filters[c][f][1][2] * rgb[(x+1)*3+p + (y+0)*width*3] +
                        filters[c][f][2][0] * rgb[(x-1)*3+p + (y+1)*width*3] + filters[c][f][2][1] * rgb[x*3+p + (y+1)*width*3] + filters[c][f][2][2] * rgb[(x+1)*3+p + (y+1)*width*3];
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

/* linear interpolation between lut[0] and lut[N] (including N)*/
static void interp1(uint16_t* lut, int N)
{
    int a = lut[0];
    int b = lut[N];
    for (int i = 1; i < N; i++)
    {
        double k = (double) i / N;
        lut[i] = round(a * (1-k) + b * k);
    }
}

static void read_lut(char * filename)
{
    /* Header looks like this:
     * 
     * Version 1
     * From 0.0 1.0
     * Length 256
     * Components 3
     */
    
    FILE* f = fopen(filename, "r");
    CHECK(f, "lut file");
    
    int version=0, length=0, components=0;
    float from_lo=0, from_hi=0;
    CHECK(fscanf(f, "Version %d\n", &version)           == 1,   "ver"    );
    CHECK(fscanf(f, "From %f %f\n", &from_lo, &from_hi) == 2,   "from"   );
    CHECK(fscanf(f, "Length %d\n", &length)             == 1,   "len"    );
    CHECK(fscanf(f, "Components %d\n", &components)     == 1,   "comp"   );
    CHECK(fscanf(f, "{\n")                              == 0,   "{"      );
    CHECK(version                                       == 1,   "ver1"   );
    CHECK(from_lo                                       == 0.0, "from_lo");
    CHECK(from_hi                                       == 1.0, "from_hi");
    
    printf("%dx%d\n", components, length);
    for (int i = 0; i < length; i++)
    {
        float r,g,b;
        switch (components)
        {
            case 1:
                CHECK(fscanf(f, "%f\n", &r), "data");
                g = b = r;
                break;
            case 3:
                CHECK(fscanf(f, "%f %f %f\n", &r, &g, &b) == 3, "data");
                break;
            default:
                printf("components error\n");
                exit(1);
        }
        
        CHECK(r >= 0 && r <= 1, "R range");
        CHECK(g >= 0 && g <= 1, "G range");
        CHECK(b >= 0 && b <= 1, "B range");
        
        int this = i * 65535 / (length-1);
        
        Lut_R[this] = (int) round(r * 65535);
        Lut_G[this] = (int) round(g * 65535);
        Lut_B[this] = (int) round(b * 65535);
        
        int prev = (i-1) * 65535 / (length-1);;
        
        if (prev >= 0)
        {
            interp1(Lut_R + prev, this - prev);
            interp1(Lut_G + prev, this - prev);
            interp1(Lut_B + prev, this - prev);
        }
    }
    CHECK(fscanf(f, "}\n")                              == 0,   "}"      );
    fclose(f);
    
    if (0)
    {
        f = fopen("lut.m", "w");
        fprintf(f, "lut = [\n");
        for (int i = 0; i < 65536; i++)
        {
            fprintf(f, "%d %d %d\n", Lut_R[i], Lut_G[i], Lut_B[i]);
        }
        fprintf(f, "];");
        fclose(f);
    }
}

static void apply_lut()
{
    int w = width;
    int h = height;

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            int r = rgb[3*x   + y*w*3];
            int g = rgb[3*x+1 + y*w*3];
            int b = rgb[3*x+2 + y*w*3];
            rgb[3*x   + y*w*3] = Lut_R[r];
            rgb[3*x+1 + y*w*3] = Lut_G[g];
            rgb[3*x+2 + y*w*3] = Lut_B[b];
        }
    }
}

static void apply_matrix()
{
    /* from config.ocio given by calib_argyll.sh, first IT8 test chart from TT9,
     * both matrices multiplied and scaled => this is HDMI to sRGB D50*/
    const float rgb_cam[3][3] = {
        {    1.59459,  -0.65713,  -0.07318 },
        {   -0.25228,   1.48252,  -0.23020 },
        {   -0.11096,  -0.62721,   1.69673 },
    };

    int w = width;
    int h = height;
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            int r = rgb[3*x   + y*w*3];
            int g = rgb[3*x+1 + y*w*3];
            int b = rgb[3*x+2 + y*w*3];
            float rr = r * rgb_cam[0][0] + g * rgb_cam[0][1] + b * rgb_cam[0][2];
            float gg = r * rgb_cam[1][0] + g * rgb_cam[1][1] + b * rgb_cam[1][2];
            float bb = r * rgb_cam[2][0] + g * rgb_cam[2][1] + b * rgb_cam[2][2];
            rgb[3*x   + y*w*3] = COERCE(rr, 0, 65535);
            rgb[3*x+1 + y*w*3] = COERCE(gg, 0, 65535);
            rgb[3*x+2 + y*w*3] = COERCE(bb, 0, 65535);
        }
    }
}

/* Apply a out_gamma curve to red, green and blue image buffers,
 * and round the values to integers between 0 and max.
 * 
 * This step also adds anti-posterization noise before rounding.
 */
static void apply_gamma()
{
    printf("Gamma...\n");

    int w = width;
    int h = height;
    double gm = 1/out_gamma;
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            double r = rgb[3*x   + y*w*3] / 65535.0;
            double g = rgb[3*x+1 + y*w*3] / 65535.0;
            double b = rgb[3*x+2 + y*w*3] / 65535.0;
            rgb[3*x   + y*w*3] = COERCE(pow(r,gm) * 65535, 0, 65535);
            rgb[3*x+1 + y*w*3] = COERCE(pow(g,gm) * 65535, 0, 65535);
            rgb[3*x+2 + y*w*3] = COERCE(pow(b,gm) * 65535, 0, 65535);
        }
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
        show_commandline_help(argv[0]);
        return 0;
    }

    /* parse all command-line options */
    for (int k = 1; k < argc; k++)
        if (argv[k][0] == '-')
            parse_commandline_option(argv[k]);
    show_active_options();

    printf("\n");
    
    char* dark_filename = "darkframe-hdmi.ppm";
    if (file_exists_warn(dark_filename))
    {
        printf("Dark frame  : %s...\n", dark_filename);
        read_ppm(dark_filename, &dark);
        use_darkframe = 1;
    }

    char* lut_filename = "lut-hdmi.spi1d";
    if (file_exists_warn(lut_filename))
    {
        /* no newline here (read_lut will print more info) */
        printf("LUT file    : %s ", lut_filename);
        read_lut(lut_filename);
        use_lut = 1;
        use_matrix = 1;
    }

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
        
        printf("Undo gamma, sub darkframe...\n");
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

        if (use_lut)
        {
            printf("Applying LUT...\n");
            apply_lut();
        }
        
        if (color_smooth_passes)
        {
            color_smooth(rgb, width, height, color_smooth_passes);
        }
        
        if (use_matrix)
        {
            printf("Applying matrix...\n");
            apply_matrix();
        }
        
        if (out_gamma != 1)
        {
            apply_gamma();
        }

        printf("Output file : %s\n", out_filename);
        write_ppm(out_filename, rgb);

        free(rgb); rgb = 0;

        if (out_4k)
        {
            width /= 2;
            height /= 2;
        }
    }
    
    printf("Done.\n\n");
    
    return 0;
}
