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
uint16_t* dark;

#define OUT_4K 0
#define OUT_1080P 1
#define OUT_1080P_FILTERED 2

/* options */
int fixpn = 0;
int fixpn_flags1;
int fixpn_flags2;
int filter1080p = 0;
int output_type = OUT_4K;
int use_darkframe = 0;
int use_lut = 0;
int use_matrix = 0;
float in_gamma = 0.5;
float out_gamma = 1;
float out_linearity = 0;
float exposure_lin = 0;
float exposure_film = 0;
int raw_offset = 0;
int ufraw_gamma = 0;
int plot_out_gamma = 0;
int color_smooth_passes = 0;
int tmp_denoise = 0;

float custom_wb[3] = {0, 0, 0};

struct cmd_group options[] = {
    {
        "Output options", (struct cmd_option[]) {
            { &output_type, OUT_1080P,          "--1080p",  "1080p output (default 4k)" },
            { &output_type, OUT_1080P_FILTERED, "--1080pf", "Filtered 1080p output (experimental)" },
            OPTION_EOL,
        },
    },
    {
        "Curve options", (struct cmd_option[]) {
            { (void*)&exposure_lin, 1, "--exposure=%f",     "Exposure compensation (EV), linear gain" },
            { (void*)&exposure_film,1, "--soft-film=%f",    "Exposure compensation (EV) using a soft-film curve" },
            { (void*)&in_gamma,     1, "--in-gamma=%f",     "Gamma value used when recording (as configured in camera)" },
            { (void*)&out_gamma,    1, "--gamma=%f",        "Gamma correction for output" },
            { (void*)&out_linearity,1, "--linearity=%f",    "Linear segment of the gamma curve" },
            { (void*)&ufraw_gamma,  1, "--ufraw-gamma",     "Use ufraw defaults: --gamma=0.45 --out-linear=0.1" },
            { (void*)&raw_offset,   1, "--offset=%d",       "Add this value after dark frame (workaround for crushed blacks)" },
            { (int*)&custom_wb[0],  3, "--wb=%f,%f,%f",     "White balance with RGB multipliers" },
            OPTION_EOL,
        },
    },
    {
        "Filtering options", (struct cmd_option[]) {
            { &color_smooth_passes, 3, "--cs",              "Apply 3 passes of color smoothing (from ufraw)" },
            { &color_smooth_passes, 1, "--cs=%d",           "Apply N passes of color smoothing (from ufraw)" },
            { &fixpn,               1, "--fixrn",           "Fix row noise by image filtering (slow, guesswork)" },
            { &fixpn,               2, "--fixpn",           "Fix row and column noise (SLOW, guesswork)" },
            { &fixpn,               3, "--fixrnt",          "Temporal row noise fix (use with static backgrounds; recommended)" },
            { &fixpn,               4, "--fixpnt",          "Temporal row/column noise fix (use with static backgrounds)" },
            { &tmp_denoise,         1, "--tdn=%d",          "Temporal denoising with adjustable strength (0-100)" },
            OPTION_EOL,
        },
    },
    {
        "Debug options", (struct cmd_option[]) {
            { &fixpn_flags1,   FIXPN_DBG_DENOISED,  "--fixpn-dbg-denoised", "Pattern noise: show denoised image" },
            { &fixpn_flags1,   FIXPN_DBG_NOISE,     "--fixpn-dbg-noise",    "Pattern noise: show noise image (original - denoised)" },
            { &fixpn_flags1,   FIXPN_DBG_MASK,      "--fixpn-dbg-mask",     "Pattern noise: show masked areas (edges and highlights)" },
            { &fixpn_flags2,   FIXPN_DBG_COLNOISE,  "--fixpn-dbg-col",      "Pattern noise: debug columns (default: rows)" },
            { &plot_out_gamma,     1,               "--plot-gamma",         "Plot the output gamma curve (requires octave)" },
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

#define ABS(a) \
   ({ __typeof__ (a) _a = (a); \
     _a > 0 ? _a : -_a; })

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
    double gain = (65535 - offset) / 40000.0;
    
    for (int i = 0; i < width*height*3; i++)
    {
        double data = rgb[i] / 65535.0;
        double dark = darkframe ? darkframe[i] / 65535.0 : 0;
        
        /* undo HDMI 16-235 scaling */
        data = data * (235.0 - 16.0) / 255.0 + 16.0 / 255.0;
        dark = dark * (235.0 - 16.0) / 255.0 + 16.0 / 255.0;
        
        /* undo gamma applied from our camera, before recording */
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
        
        /* subtract dark frame, scale the (now linear) values to cover the full range
         * and add an offset to avoid crushed blacks and clamp values */
        rgb[i] = COERCE((data - dark) * 65535 * gain + offset, 0, 65535);
    }
}

/**
 * We'll use these filters to extract details from other color channels,
 * basically solving a disguised demosaicing problem.
 * 
 * Hypothesis: on areas with solid color, ratios between color channels are constant.
 * That means, we should work on linear data (either RGB or camera native, without any offsets).
 * 
 * To match two color channels, we'll apply a low-pass filter on both channels,
 * and scale the predictor channel to match the recovered channel,
 * with the inverse of their filtered versions' ratio (per-pixel).
 * 
 * After that step, we can borrow high-frequency details from the other channels
 * using these filters.
 * 
 * The filters were obtained from optimization on a pair of test images:
 * raw12 + HDMI frame, both from some static scene, with pixel-perfect alignment.
 * 
 * There's still a lot of room for improvement here.
 */

/* fspecial('gaussian', 5, 2) * 8192 */
/* see http://stackoverflow.com/questions/25216834/converting-2d-mask-to-1d-in-gaussian-blur */
const int gaussian_s2_sep5[] = { 1249, 1817, 2059, 1817, 1249 };

const int filters_1080p[3][3][3][3] = {
    /* Red: */
    {
        /* from red: */
        {
            {    89,  938,  156 },
            {   180, 2981,  395 },
            {  -259,  115,   63 },
        },
        /* from green: */
        {
            {  -617,  458,  599 },
            {   410, 3046, 1209 },
            {    -6,   -4, -167 },
        },
        /* from blue: */
        {
            {  -168, -380, -339 },
            {   -32,  355, -453 },
            {   147,   36, -558 },
        },
    },
    /* Green: */
    {
        /* from red: */
        {
            {   -87,  454,  312 },
            {   -49, 2453,  115 },
            {    37, -205,  203 },
        },
        /* from green: */
        {
            {  -561,   74, -231 },
            {   386, 3268,  145 },
            {   -12,   64, -676 },
        },
        /* from blue: */
        {
            {    94,  -46,  -20 },
            {   331, 1005,  259 },
            {   244,  525,  121 },
        },
    },
    /* Blue: */
    {
        /* from red: */
        {
            {  -366,  326,  214 },
            {  -376, 2148,  -80 },
            {  -128, -228, -118 },
        },
        /* from green: */
        {
            {   189, -171, -422 },
            {  1342, 2858,  132 },
            {  1151, -115, -543 },
        },
        /* from blue: */
        {
            {  -231,  184, -234 },
            {   100, 1375,  280 },
            {  -108,  787,  221 },
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
                {   496, 1569, -368 },
                {   188, 2358, -240 },
                {  -123,  236, -112 },
            },
            /* from green: */
            {
                {  -352,  995, 1146 },
                {   635, 3898,  315 },
                {  -325,  -68,  356 },
            },
            /* from blue: */
            {
                {  -262, -545, -465 },
                {   396,  471, -522 },
                {  -220, -629, -662 },
            },
        },
        /* Green: */
        {
            /* from red: */
            {
                {   212, 1131, -211 },
                {    -7, 1902, -412 },
                {   212,   -0,  160 },
            },
            /* from green: */
            {
                {  -291,  454,  276 },
                {   722, 4080, -752 },
                {  -362, -237,  -99 },
            },
            /* from blue: */
            {
                {    13, -175, -124 },
                {   821, 1168,  168 },
                {  -193, -142, -141 },
            },
        },
        /* Blue: */
        {
            /* from red: */
            {
                {   -59,  947, -281 },
                {  -353, 1598, -631 },
                {    63,   -7, -184 },
            },
            /* from green: */
            {
                {   556,   56,  171 },
                {  1741, 3557, -544 },
                {   758, -507,  137 },
            },
            /* from blue: */
            {
                {  -343,   75, -404 },
                {   599, 1553,  139 },
                {  -512,  109,  -79 },
            },
        },
    },
    /* Sub-image #2 (0,1): */
    {
        /* Red: */
        {
            /* from red: */
            {
                {   -74, 1562,  503 },
                {  -180, 1538,  998 },
                {  -369,  368,  -49 },
            },
            /* from green: */
            {
                {  -627, 1036,  947 },
                {   372, 3219,  477 },
                {   368,-1165,  147 },
            },
            /* from blue: */
            {
                {  -252, -623, -382 },
                {  -433, 1252,  353 },
                {   137, -105, -827 },
            },
        },
        /* Green: */
        {
            /* from red: */
            {
                {  -199,  921,  550 },
                {  -222, 1021,  730 },
                {     3,  130,   86 },
            },
            /* from green: */
            {
                {  -675,  928,  -89 },
                {   166, 3856, -706 },
                {   290,-1247, -243 },
            },
            /* from blue: */
            {
                {    26, -298,  -14 },
                {  -148, 1858, 1252 },
                {   186,  225, -186 },
            },
        },
        /* Blue: */
        {
            /* from red: */
            {
                {  -431,  774,  479 },
                {  -515,  745,  508 },
                {  -175,  195, -159 },
            },
            /* from green: */
            {
                {   109,  771, -502 },
                {  1091, 3647, -788 },
                {  1284,-1327, -194 },
            },
            /* from blue: */
            {
                {  -342,  -85, -202 },
                {  -409, 2203, 1363 },
                {  -158,  421, -113 },
            },
        },
    },
    /* Sub-image #3 (1,0): */
    {
        /* Red: */
        {
            /* from red: */
            {
                {  -188, -860,  159 },
                {  1202, 5894,-1495 },
                {  -595, -425,   -9 },
            },
            /* from green: */
            {
                {  -367,   88,  541 },
                {  -285, 4327, 1704 },
                {   387,  504,  252 },
            },
            /* from blue: */
            {
                {  -527,  156, -287 },
                {   119, -680,-1001 },
                {   434, -317, -549 },
            },
        },
        /* Green: */
        {
            /* from red: */
            {
                {  -314,-1180,  453 },
                {   824, 5407,-1764 },
                {  -369, -749,  148 },
            },
            /* from green: */
            {
                {  -227, -631,  -77 },
                {  -326, 4289,  671 },
                {   542,  539, -308 },
            },
            /* from blue: */
            {
                {  -310,  515,  -37 },
                {   554,  -35, -439 },
                {   591,  301,  122 },
            },
        },
        /* Blue: */
        {
            /* from red: */
            {
                {  -652,-1210,  277 },
                {   494, 5023,-1890 },
                {  -528, -868, -231 },
            },
            /* from green: */
            {
                {   530, -961,    4 },
                {   653, 3643,  782 },
                {  1893,  337,  -84 },
            },
            /* from blue: */
            {
                {  -548,  722, -331 },
                {   249,  388, -562 },
                {   204,  579,  255 },
            },
        },
    },
    /* Sub-image #4 (1,1): */
    {
        /* Red: */
        {
            /* from red: */
            {
                {   -60, -123,  133 },
                {  -110, 2930, 1334 },
                {  -276,  346,   -1 },
            },
            /* from green: */
            {
                {     2, -438,  120 },
                {  -120, 3409, 1368 },
                {    52,  514, -231 },
            },
            /* from blue: */
            {
                {    48,  -14,   24 },
                {  -620,   73, -501 },
                {   -25,  508, -124 },
            },
        },
        /* Green: */
        {
            /* from red: */
            {
                {  -104, -619,  300 },
                {  -301, 2360,  996 },
                {    49,  -34,   70 },
            },
            /* from green: */
            {
                {    25, -680, -724 },
                {  -296, 3786,  275 },
                {   -13,  859, -830 },
            },
            /* from blue: */
            {
                {   317,  269,  343 },
                {  -335,  615,  194 },
                {    64,  946,  692 },
            },
        },
        /* Blue: */
        {
            /* from red: */
            {
                {  -355, -697,  225 },
                {  -625, 2112,  808 },
                {  -112,  -70, -242 },
            },
            /* from green: */
            {
                {   659, -722, -948 },
                {   658, 3444,  149 },
                {  1199,  796, -765 },
            },
            /* from blue: */
            {
                {   -57,  436,  164 },
                {  -565,  911,  195 },
                {  -364, 1205,  773 },
            },
        },
    },
};

/* separable 5x5 filter */
static void rgb_filter_sep5(uint16_t* img, int w, int h, const int filter[5])
{
    int size = w * h * 3 * sizeof(int32_t);
    int32_t * aux = malloc(size);

    for (int i = 0; i < w * h * 3; i++)
    {
        aux[i] = img[i] * 8192;
    }
    
    /* apply the filter on the X direction */
    for (int y = 0; y < h; y++)
    {
        for (int x = 2; x < w-2; x++)
        {
            for (int c = 0; c < 3; c++)
            {
                aux[x*3+c + y*width*3] =
                    filter[0] * img[(x-2)*3+c + y*width*3] +
                    filter[1] * img[(x-1)*3+c + y*width*3] + 
                    filter[2] * img[(x+0)*3+c + y*width*3] +
                    filter[3] * img[(x+1)*3+c + y*width*3] + 
                    filter[4] * img[(x+2)*3+c + y*width*3] ;
            }
        }
    }

    for (int i = 0; i < width * height * 3; i++)
    {
        img[i] = COERCE(aux[i] / 8192, 0, 65535);
    }

    /* same for the Y direction */
    for (int x = 0; x < w; x++)
    {
        for (int y = 2; y < h-2; y++)
        {
            for (int c = 0; c < 3; c++)
            {
                aux[x*3+c + y*width*3] =
                    filter[0] * img[x*3+c + (y-2)*width*3] +
                    filter[1] * img[x*3+c + (y-1)*width*3] + 
                    filter[2] * img[x*3+c + (y+0)*width*3] +
                    filter[3] * img[x*3+c + (y+1)*width*3] + 
                    filter[4] * img[x*3+c + (y+2)*width*3] ;
            }
        }
    }

    for (int i = 0; i < width * height * 3; i++)
    {
        img[i] = COERCE(aux[i] / 8192, 0, 65535);
    }
    
    free(aux);
}

/* adjust all color channels to match ch */
static void rgb_match_to_channel(uint16_t* img, int ch, int w, int h)
{
    int size = w * h * 3 * sizeof(uint16_t);
    uint16_t * blr = malloc(size);

    /* low-pass filter: fspecial('gaussian', 5, 2) */
    memcpy(blr, img, size);
    rgb_filter_sep5(blr, width, height, gaussian_s2_sep5);

    /* use the filtered image ratio as the scaling factor (per pixel) */
    for (int c = 0; c < 3; c++)
    {
        if (c != ch)
        {
            for (int i = 0; i < w*h; i++)
            {
                img[i*3+c] = COERCE((int64_t) img[i*3+c] * blr[i*3+ch] / MAX(blr[i*3+c], 1), 0, 65535);
            }
        }
    }
    
    free(blr);
}

/* operate on RGB images */
/* filter one channel from 3 predictors */
static void rgb_filter_3x3_1ch_3p(uint16_t* out, uint16_t* pred, int c, int w, int h, const int filters[3][3][3])
{
    int size = w * h * 3 * sizeof(int32_t);
    int32_t * aux = malloc(size);
    memset(aux, 0, size);

    for (int y = 1; y < h-1; y++)
    {
        for (int x = 1; x < w-1; x++)
        {
            /* compute channel c from predictor channel p */
            for (int p = 0; p < 3; p++)
            {
                 aux[x*3+c + y*width*3] +=
                    filters[p][0][0] * pred[(x-1)*3+p + (y-1)*width*3] + filters[p][0][1] * pred[x*3+p + (y-1)*width*3] + filters[p][0][2] * pred[(x+1)*3+p + (y-1)*width*3] +
                    filters[p][1][0] * pred[(x-1)*3+p + (y+0)*width*3] + filters[p][1][1] * pred[x*3+p + (y+0)*width*3] + filters[p][1][2] * pred[(x+1)*3+p + (y+0)*width*3] +
                    filters[p][2][0] * pred[(x-1)*3+p + (y+1)*width*3] + filters[p][2][1] * pred[x*3+p + (y+1)*width*3] + filters[p][2][2] * pred[(x+1)*3+p + (y+1)*width*3] ;
            }
        }
    }

    for (int i = 0; i < w * h; i++)
    {
        out[i*3+c] = COERCE(aux[i*3+c] / 8192, 0, 65535);
    }
    
    free(aux);
}

static void rgb_filters_debayer(uint16_t* img, const int filters[3][3][3][3])
{
    int size = width * height * 3 * sizeof(uint16_t);
    uint16_t * img0 = malloc(size);
    uint16_t * adj = malloc(size);
    memcpy(img0, img, size);

    for (int ch = 0; ch < 3; ch++)
    {
        memcpy(adj, img0, size);
        rgb_match_to_channel(adj, ch, width, height);
        rgb_filter_3x3_1ch_3p(img, adj, ch, width, height, filters[ch]);
    }
    
    free(adj);
    free(img0);
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

static void rgb_filters_debayer_x2(const int filters_4k[4][3][3][3][3])
{
    int size = width * height * 3 * sizeof(uint16_t);
    uint16_t * rgbf = malloc(size);
    uint16_t* rgbx2 = malloc(size * 4);

    uint16_t * adj_r = malloc(size);
    uint16_t * adj_g = malloc(size);
    uint16_t * adj_b = malloc(size);
    uint16_t * adj[] = { adj_r, adj_g, adj_b };
    
    /* adjust color channels to match red, green and blue */
    for (int ch = 0; ch < 3; ch++)
    {
        memcpy(adj[ch], rgb, size);
        rgb_match_to_channel(adj[ch], ch, width, height);
    }
    
    /* recover the details by filtering the adjusted color channels */
    for (int k = 0; k < 4; k++)
    {
        memcpy(rgbf, rgb, size);
        
        for (int ch = 0; ch < 3; ch++)
        {
            rgb_filter_3x3_1ch_3p(rgbf, adj[ch], ch, width, height, filters_4k[k][ch]);
        }
        
        copy_subimage(rgbx2, rgbf, k%2, k/2);
    }
    
    /* replace output buffer with a double-res one */
    free(rgb);
    rgb = rgbx2;
    width *= 2;
    height *= 2;
    
    /* that's it! */
    free(rgbf);
    free(adj_r);
    free(adj_g);
    free(adj_b);
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
        {    1.16113,  -0.53352,  -0.07452 },
        {   -0.18029,   1.25180,  -0.21653 },
        {   -0.07405,  -0.52073,   1.59754 },
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

static void white_balance(float multipliers[3])
{
    printf("White balance %.2f,%.2f,%.2f...\n", multipliers[0], multipliers[1], multipliers[2]);
    int w = width;
    int h = height;
    for (int y = 0; y < h; y ++)
    {
        for (int x = 0; x < w; x ++)
        {
            for (int ch = 0; ch < 3; ch++)
            {
                rgb[3*x+ch + y*w*3] = COERCE(rgb[3*x+ch + y*w*3] * multipliers[ch], 0, 65535);
            }
        }
    }
}

static int gamma_curve[0x10000];

/* gamma curves borrowed from ufraw */
/* exposure is linear, 1.0 = normal */
static void setup_gamma_curve(double linear, double gamma, double exposure_lin, double exposure_film)
{
    float FilmCurve[0x10000];
    {
        /* Exposure is set by FilmCurve[].
         * Set initial slope to exposure (in linear units)
         */
        double a = exposure_film - 1;
        if (ABS(a) < 1e-5) a = 1e-5;
        for (int i = 0; i < 0x10000; i++) {
            double x = (double) i * exposure_lin / 0x10000;
            FilmCurve[i] = COERCE((1 - 1/(1+a*x)) / (1 - 1/(1+a)) * 0xFFFF, 0, 0xFFFF);
        }
    }
    {
        double a, b, c, g;
        /* The parameters of the linearized gamma curve are set in a way that
         * keeps the curve continuous and smooth at the connecting point.
         * d->linear also changes the real gamma used for the curve (g) in
         * a way that keeps the derivative at i=0x10000 constant.
         * This way changing the linearity changes the curve behaviour in
         * the shadows, but has a minimal effect on the rest of the range. */
        if (linear < 1.0) {
            g = gamma * (1.0 - linear) / (1.0 - gamma * linear);
            a = 1.0 / (1.0 + linear * (g - 1));
            b = linear * (g - 1) * a;
            c = pow(a * linear + b, g) / linear;
        } else {
            a = b = g = 0.0;
            c = 1.0;
        }
        for (int i = 0; i < 0x10000; i++)
        {
            if (FilmCurve[i] < 0x10000 * linear)
                gamma_curve[i] = MIN(round(c * FilmCurve[i]), 0xFFFF);
            else
                gamma_curve[i] = MIN(round(pow(a * FilmCurve[i] / 0x10000 + b,
                                           g) * 0x10000), 0xFFFF);
        }
    }
    
    if (plot_out_gamma)
    {
        FILE* f = fopen("gamma.m", "w");
        fprintf(f, "g = [");
        for (int i = 0; i < 0x10000; i++)
        {
            fprintf(f, "%d ", gamma_curve[i]);
        }
        fprintf(f, "];\n");
        fprintf(f, "x = 0:length(g)-1;");
        fprintf(f, "loglog(x, g, 'o-b', 'markersize', 2, x, x, '-r'); grid on;\n");
        fclose(f);
        if(system("octave --persist gamma.m"));
    }
}

/* Apply a gamma curve to red, green and blue */
static void rgb_apply_gamma_curve(int preserve_hue)
{
    printf("Gamma...\n");

    for (int y = 0; y < height; y ++)
    {
        for (int x = 0; x < width; x ++)
        {
            int r = rgb[3*x   + y*width*3];
            int g = rgb[3*x+1 + y*width*3];
            int b = rgb[3*x+2 + y*width*3];

            r = gamma_curve[r];
            g = gamma_curve[g];
            b = gamma_curve[b];

            rgb[3*x   + y*width*3] = r;
            rgb[3*x+1 + y*width*3] = g;
            rgb[3*x+2 + y*width*3] = b;
        }
    }
}

/* temporal pattern denoising */
struct
{
    int32_t * rgb32;
    int count;
} A;

static void moving_average_addframe(uint16_t * rgb16)
{
    int n = width * height * 3;
    int frame_size = n * sizeof(A.rgb32[0]);
    
    if (!A.rgb32)
    {
        /* allocate memory on first call */
        A.rgb32 = malloc(frame_size);
        CHECK(A.rgb32, "malloc");

        for (int i = 0; i < n; i++)
        {
            A.rgb32[i] = rgb16[i] * 1024;
        }
    }
        
    /* add current frame to accumulator */
    for (int i = 0; i < n; i++)
    {
        int pix = (int) rgb16[i] * 1024;
        int avg = A.rgb32[i];
        int dif = ABS(pix - avg);
        if (dif > 500 * 1024)
        {
            /* reset moving average if difference gets too high (probably motion) */
            A.rgb32[i] = pix;
        }
        else
        {
            A.rgb32[i] = A.rgb32[i] * 7/8 + pix * 1/8;
        }
    }
    A.count++;
}

static void fix_pattern_noise_temporally(int fixpn_flags)
{
    int n = width * height * 3;
    uint16_t* avg = malloc(n * sizeof(avg[0]));
    
    for (int i = 0; i < n; i++)
    {
        avg[i] = (A.rgb32[i] + 512) / 1024;
    }
    
    fix_pattern_noise_ex(rgb, avg, width, height, fixpn & 1, fixpn_flags);
    
    free(avg);
}

static void temporal_denoise()
{
    int n = width * height * 3;
    float k = COERCE(tmp_denoise / 100.0, 0, 1);
    for (int i = 0; i < n; i++)
    {
        int avg = (A.rgb32[i] + 512) / 1024;
        rgb[i] = rgb[i] * (1-k) + avg * k;
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
        printf("Calibration files:\n");
        printf("  hdmi-darkframe.ppm: averaged dark frames from the HDMI recorder\n");
        printf("  lut-hdmi.spi1d    : linearization LUT (created with calib_argyll.sh)\n");
        printf("\n");
        printf("Note: when using LUT, a (hardcoded) color matrix is also applied.");
        show_commandline_help(argv[0]);
        return 0;
    }

    /* parse all command-line options */
    for (int k = 1; k < argc; k++)
        if (argv[k][0] == '-')
            parse_commandline_option(argv[k]);
    show_active_options();
    
    if (ufraw_gamma)
    {
        out_gamma = 0.45;
        out_linearity = 0.1;
    }

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
        printf("Color matrix: on");
        use_matrix = 1;
    }
    
    setup_gamma_curve(out_linearity, out_gamma, pow(2,exposure_lin), pow(2,exposure_film));

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
        convert_to_linear_and_subtract_darkframe(rgb, dark, 1024 + raw_offset);

        if (fixpn == 3 || fixpn == 4 || tmp_denoise)
        {
            moving_average_addframe(rgb);
        }
                
        if (tmp_denoise)
        {
            printf("Temporal denoising...\n");
            temporal_denoise();
        }
        
        if (fixpn)
        {
            int fixpn_flags = fixpn_flags1 | fixpn_flags2;
            if (fixpn == 3 || fixpn == 4)
            {
                if (tmp_denoise >= 100)
                {
                    printf("Warning: temporal pattern noise reduction has no effect.");
                }
                else
                {
                    fix_pattern_noise_temporally(fixpn_flags);
                }
            }
            else
            {
                fix_pattern_noise(rgb, width, height, fixpn & 1, fixpn_flags);
            }
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
        
        if (custom_wb[0] || custom_wb[1] || custom_wb[2])
        {
            white_balance(custom_wb);
        }

        if (output_type == OUT_1080P_FILTERED)
        {
            printf("Filtering image...\n");
            rgb_filters_debayer(rgb, filters_1080p);
        }
        
        if (output_type == OUT_4K)
        {
            printf("Filtering 4K...\n");
            rgb_filters_debayer_x2(filters_4k);
        }

        if (use_matrix)
        {
            printf("Applying matrix...\n");
            apply_matrix();
        }
        
        if (out_gamma != 1 || exposure_lin != 0 || exposure_film != 0)
        {
            rgb_apply_gamma_curve(0);
        }

        printf("Output file : %s\n", out_filename);
        write_ppm(out_filename, rgb);

        free(rgb); rgb = 0;

        if (output_type == OUT_4K)
        {
            width /= 2;
            height /= 2;
        }
    }
    
    printf("Done.\n\n");
    
    return 0;
}
