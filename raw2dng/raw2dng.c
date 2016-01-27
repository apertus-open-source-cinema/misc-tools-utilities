/*
 * Copyright (C) 2013 Magic Lantern Team
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
#include "raw.h"
#include "chdk-dng.h"
#include "cmdoptions.h"
#include "patternnoise.h"
#include "metadata.h"
#include "wirth.h"

static int16_t Lut_R[4096*8];
static int16_t Lut_G1[4096*8];
static int16_t Lut_G2[4096*8];
static int16_t Lut_B[4096*8];

/* matched colorchecker_gainx2_15ms_01.raw12 (linearized and pattern noise corrected)
 * with ColorcheckerPassport NIKON.NEF (Nikon D800E) */
#define CAM_COLORMATRIX1                          \
   11038, 10000,    -3184, 10000,   -1009, 10000, \
   -3284, 10000,    11499, 10000,    1737, 10000, \
   -1283, 10000,     3550, 10000,    5967, 10000

#define DARKFRAME_OFFSET 1024
#define GAINFRAME_SCALING 16384
#define DCNUFRAME_OFFSET 8192
#define DCNUFRAME_SCALING 8192

/**
 * How much the dark frame average, after subtracting black reference columns, 
 * increases with exposure time and gain (DN / ms / gain);
 */
float dark_current_avg = 0.06;

int black_level = 128;
int white_level = 4095;
int image_width = 0;
int image_height = 0;
int swap_lines = 0;
int hdmi_ramdump = 0;
int use_lut = 0;
int fixpn = 0;
int fixpn_flags1 = 0;
int fixpn_flags2 = 0;
int dump_regs = 0;
int no_darkframe = 0;
int no_dcnuframe = 0;
int no_gainframe = 0;
int no_clipframe = 0;
int no_blackcol = 0;
int no_processing = 0;

int calc_darkframe = 0;
int calc_clipframe = 0;
int calc_gainframe = 0;
int calc_dcnuframe = 0;

struct cmd_group options[] = {
    {
        "General options", (struct cmd_option[]) {
            { &black_level,    1, "--black=%d",    "Set black level (default: 128)\n"
                             "                      - negative values allowed" },
            { &white_level,    1, "--white=%d",    "Set white level (default: 4095)\n"
                             "                      - if too high, you may get pink highlights\n"
                             "                      - if too low, useful highlights may clip to white" },
            { &image_width,   1,  "--width=%d",    "Set image width (default: 4096)"},
            { &image_height,   1, "--height=%d",   "Set image height\n"
                             "                      - default: autodetect from file size\n"
                             "                      - if input is stdin, default is 3072" },
            { &swap_lines,     1,  "--swap-lines", "Swap lines in the raw data\n"
                             "                      - workaround for an old Beta bug" },
            { &hdmi_ramdump,   1,  "--hdmi",       "Assume the input is a memory dump\n"
                             "                      used for HDMI recording experiments" },
            { &use_lut,        1,  "--lut",        "Use a 1D LUT (lut-xN.spi1d, N=gain, OCIO-like)\n" },
            { &fixpn,          1,  "--fixrn",      "Fix row noise (slow)" },
            { &fixpn,          2,  "--fixpn",      "Fix row and column noise, aka pattern noise (SLOW)" },
            { &no_processing,  1, "--totally-raw", "Copy the raw data without any manipulation\n"
                             "                      - metadata and pixel reordering are allowed." },
            OPTION_EOL,
        },
    },
    {
        "Flat field correction", (struct cmd_option[]) {
            { &no_darkframe,   1,"--no-darkframe", "Disable dark frame (if darkframe-xN.pgm is present)" },
            { &no_dcnuframe,   1,"--no-dcnuframe", "Disable dark current frame (if dcnuframe-xN.pgm is present)" },
            { &no_gainframe,   1,"--no-gainframe", "Disable gain frame (if gainframe-xN.pgm is present)" },
            { &no_clipframe,   1,"--no-clipframe", "Disable clip frame (if clipframe-xN.pgm is present)" },
            { &no_blackcol,    1,"--no-blackcol",  "Disable black reference column subtraction\n"
                             "                      - enabled by default if a dark frame is used\n"
                             "                      - reduces row noise and black level variations" },

            { &calc_darkframe,1,"--calc-darkframe","Average a dark frame from all input files" },
            { &calc_dcnuframe,1,"--calc-dcnuframe","Fit a dark frame (constant offset) and a dark current frame\n"
                             "                      (exposure-dependent offset) from files with different exposures\n"
                             "                      (starting point: 256 frames with exposures from 1 to 50 ms)" },
            { &calc_gainframe,1,"--calc-gainframe","Average a gain frame (aka flat field frame)" },
            { &calc_clipframe,1,"--calc-clipframe","Average a clip (overexposed) frame" },
            OPTION_EOL,
        },
    },
    {
        "Debug options", (struct cmd_option[]) {
            { &dump_regs,      1,                   "--dump-regs",          "Dump sensor registers from metadata block (no output DNG)" },
            { &fixpn_flags1,   FIXPN_DBG_DENOISED,  "--fixpn-dbg-denoised", "Pattern noise: show denoised image" },
            { &fixpn_flags1,   FIXPN_DBG_NOISE,     "--fixpn-dbg-noise",    "Pattern noise: show noise image (original - denoised)" },
            { &fixpn_flags1,   FIXPN_DBG_MASK,      "--fixpn-dbg-mask",     "Pattern noise: show masked areas (edges and highlights)" },
            { &fixpn_flags2,   FIXPN_DBG_ROWNOISE,  "--fixpn-dbg-row",      "Pattern noise: debug rows (default: columns)" },
            OPTION_EOL,
        },
    },
    OPTION_GROUP_EOL
};

struct raw_info raw_info;

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

#define COUNT(x)        ((int)(sizeof(x)/sizeof((x)[0])))

void raw_set_geometry(int width, int height, int skip_left, int skip_right, int skip_top, int skip_bottom)
{
    raw_info.width = width;
    raw_info.height = height;
    raw_info.pitch = raw_info.width * raw_info.bits_per_pixel / 8;
    raw_info.frame_size = raw_info.height * raw_info.pitch;
    raw_info.active_area.x1 = skip_left;
    raw_info.active_area.y1 = skip_top;
    raw_info.active_area.x2 = raw_info.width - skip_right;
    raw_info.active_area.y2 = raw_info.height - skip_bottom;
    raw_info.jpeg.x = 0;
    raw_info.jpeg.y = 0;
    raw_info.jpeg.width = raw_info.width - skip_left - skip_right;
    raw_info.jpeg.height = raw_info.height - skip_top - skip_bottom;
}

struct raw_info raw_info = {
    .api_version = 1,
    .bits_per_pixel = 12,
    .black_level = 0,
    .white_level = 4095,

    // The sensor bayer patterns are:
    //  0x02010100  0x01000201  0x01020001  0x00010102
    //      R G         G B         G R         B G
    //      G B         R G         B G         G R
    .cfa_pattern = 0x01000201,

    .calibration_illuminant1 = 1,       // Daylight
    .color_matrix1 = {CAM_COLORMATRIX1},// camera-specific, from dcraw.c
};

/* apply a constant offset to each raw12 pixel */
static void raw12_data_offset(void* buf, int frame_size, int offset)
{
    struct raw12_twopix * buf2 = (struct raw12_twopix *) buf;
    for (int i = 0; i < frame_size / sizeof(struct raw12_twopix); i ++)
    {
        unsigned a = (buf2[i].a_hi << 4) | buf2[i].a_lo;
        unsigned b = (buf2[i].b_hi << 8) | buf2[i].b_lo;
        
        a = MIN(a + offset, 4095);
        b = MIN(b + offset, 4095);
        
        buf2[i].a_lo = a; buf2[i].a_hi = a >> 4;
        buf2[i].b_lo = b; buf2[i].b_hi = b >> 8;
    }
}

/* compute offset for odd/even rows, at left and right of the frame, and average offset */
static void calc_black_columns_offset(struct raw_info * raw_info, int16_t * raw16, int offsets[4], int* avg_offset)
{
    int w = raw_info->width;
    int h = raw_info->height;

    int max_samples = h / 2;
    int* samples[4];
    for (int i = 0; i < 4; i++)
    {
        samples[i] = malloc(max_samples * sizeof(samples[0][0]));
    }
    int num_samples[4] = {0, 0, 0, 0};

    for (int y = 0; y < h; y++)
    {
        /* by trial and error, it seems to minimize stdev(row_noise)
         * if we compute median horizontally on every 8 columns,
         * then median on odd/even rows from the resulting medians.
         */
        int row[8];
        for (int x = 0; x < 8; x++)
        {
            row[x] = raw16[x + y*w];
        }
        samples[y%2][num_samples[y%2]++] = median_int_wirth2(row, 8);

        for (int x = w-8; x < w; x++)
        {
            row[x-w+8] = raw16[x + y*w];
        }
        samples[2+y%2][num_samples[2+y%2]++] = median_int_wirth2(row, 8);
    }

    for (int i = 0; i < 4; i++)
    {
        offsets[i] = median_int_wirth2(samples[i], num_samples[i]);
    }

    for (int i = 0; i < 4; i++)
    {
        free(samples[i]);
    }

    *avg_offset = (offsets[0] + offsets[1] + offsets[2] + offsets[3] + 2) / 4;
}

static void subtract_black_columns(struct raw_info * raw_info, int16_t * raw16)
{
    int w = raw_info->width;
    int h = raw_info->height;

    int offsets[4];
    int avg_offset_unused;
    int target_black_level = 128 * 8;

    calc_black_columns_offset(raw_info, raw16, offsets, &avg_offset_unused);

    printf("Even rows   : %d...%d\n", offsets[0]/8, offsets[2]/8);
    printf("Odd rows    : %d...%d\n", offsets[1]/8, offsets[3]/8);
    
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            int off_l = offsets[y%2];
            int off_r = offsets[2 + y%2];
            int off = off_l + (off_r - off_l) * x / w - target_black_level;
            raw16[x + y*w] -= off;
        }
    }
        
    printf("Row noise from black columns...\n");
    /**
     * Do not subtract the full black column variations. Here's why:
     * 
     * Kalman filter theory: http://robocup.mi.fu-berlin.de/buch/kalman.pdf
     * 
     * From page 3, if we know how noisy our estimations are,
     * the optimal weights are inversely proportional with the noise variances:
     * 
     * x_optimal = (x1 * var(x2) + x2 * var(x1)) / (var(x1) + var(x2))
     * 
     * Here, let's say R = x1 is row noise (stdev = 1.6 at gain=x1) and x2 is
     * black column noise: B = mean(black_col') = R + x2 => x2 = B - R,
     * x2 can be estimated as mean(black_col') - mean(active_area'),
     * stdev(x2) = 1.3.
     * 
     * We want to find k that minimizes var(R - k*B).
     * 
     * var(R - k*B) = var(x1 * (1-k) - x2 * k),
     * so k = var(x1)) / (var(x1) + var(x2).
     */
    /* fixme: values only valid for gain=x1 */
    float row_noise_std = 1.6;
    float black_col_noise_std = 1.3;
    float blackcol_ratio = row_noise_std*row_noise_std /
        (row_noise_std*row_noise_std + black_col_noise_std*black_col_noise_std);
    
    int* row_noise = malloc(h * sizeof(row_noise[0]));
    
    for (int y = 0; y < h; y++)
    {
        int acc = 0;
        for (int x = 0; x < w; x++)
        {
            if (x == 8)
            {
                /* fast forward to the right side */
                x = w - 8;
            }
            acc += raw16[x + y*w] - target_black_level;
        }
        row_noise[y] = round(acc * blackcol_ratio / 16);
    }
    
    int acc = 0;
    for (int y = 0; y < h; y++)
    {
        acc += row_noise[y];
    }
    int row_noise_mean = (acc + h/2) / h;

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            raw16[x + y*w] -= row_noise[y] - row_noise_mean;
        }
    }
    
    free(row_noise);
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

/* for dark frames, clip frames, gray frames, stuff like that */
static void read_reference_frame(char* filename, int16_t * buf, struct raw_info * raw_info, int meta_ystart)
{
    FILE* fp = fopen(filename, "rb");
    CHECK(fp, "could not open %s", filename);

    /* PGM read code from dcraw */
    int dim[3]={0,0,0}, comment=0, number=0, error=0, nd=0, c;

      if (fgetc(fp) != 'P' || fgetc(fp) != '5') error = 1;
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

    int width = dim[0];
    int height = dim[1];
    
    if (width != raw_info->width || height != raw_info->height)
    {
        printf("%s: size mismatch, expected %dx%d, got %dx%d.\n",
            filename, raw_info->width, raw_info->height, width, height
        );
        exit(1);
    }

    int size = fread(buf, 1, width * height * 2, fp);
    CHECK(size == width * height * 2, "fread");
    fclose(fp);

    /* PGM is big endian, need to reverse it */
    reverse_bytes_order((void*)buf, width * height * 2);
    
    if (meta_ystart)
    {
        /* if our frame is cropped, skip as many lines as we need, assuming
         * the calibration frames are always full-resolution */
        memcpy(buf, buf + meta_ystart * width * 2, (width - meta_ystart) * height * 2);
    }
}

static void subtract_dark_frame(struct raw_info * raw_info, int16_t * raw16, int16_t * darkframe, int16_t extra_offset, int16_t * dcnu, float meta_expo)
{
    /* note: data in dark frames is multiplied by 8 (already done when promoting to raw16)
     * and offset by DARKFRAME_OFFSET, to allow corrections below black level */
    int w = raw_info->width;
    int h = raw_info->height;
    int dark_current = extra_offset;
    
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            int i = x + y*w;
            
            if (dcnu)
            {
                float dc = (float) (dcnu[i] - DCNUFRAME_OFFSET) * 8 / DCNUFRAME_SCALING;
                dark_current = (int)roundf(dc * meta_expo);
            }
            
            if (x >= 8 && x < w - 8)
            {
                /* for the active area, subtract the dark frame (constant offset)
                 * and the dark current (exposure-dependent offset) */
                raw16[i] -= (darkframe[i] - DARKFRAME_OFFSET + dark_current);
            }
            else
            {
                /* for black columns, subtract only the dark frame, not the dark current
                 * (because that's how we define the dark current: the dark frame variation
                 * with exposure after subtracting the black columns) */
                raw16[i] -= (darkframe[i] - DARKFRAME_OFFSET);
            }
        }
    }
}

static void apply_gain_frame(struct raw_info * raw_info, int16_t * raw16, uint16_t * dark)
{
    int black = raw_info->black_level;
    int n = raw_info->width * raw_info->height;
    for (int i = 0; i < n; i++)
    {
        raw16[i] = MIN((int64_t)(raw16[i] - black) * dark[i] / GAINFRAME_SCALING + black, 32760);
    }
}

static void apply_clip_frame(struct raw_info * raw_info, int16_t * raw16, uint16_t * clip)
{
    /* todo: use median? */
    double clip_avg = 0;
    int w = raw_info->width;
    int h = raw_info->height;

    for (int y = 0; y < h; y++)
    {
        for (int x = 8; x < w-8; x++)
        {
            clip_avg += clip[x + y*w];
        }
    }
    clip_avg /= (w - 16) * h;
    
    /* fixme: magic numbers hardcoded for gain x1 */
    int n = raw_info->width * raw_info->height;
    for (int i = 0; i < n; i++)
    {
        if (raw16[i] > 2500 * 8)
        {
            /* subtract clip frame from clipped highlights, preserving the average value */
            raw16[i] -= clip[i] - clip_avg;
        }
        else if (raw16[i] > 2000 * 8)
        {
            /* transition to clipped highlights (not sure it's the right way, but...) */
            raw16[i] -= (clip[i] - clip_avg) * 500 / (raw16[i] - 2000);
        }
    }
}

/* linear interpolation between lut[0] and lut[N] (including N)*/
static void interp1(int16_t* lut, int N)
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
        float r,g1,g2,b;
        switch (components)
        {
            case 1:
                CHECK(fscanf(f, "%f\n", &r), "data");
                g1 = g2 = b = r;
                break;
            case 3:
                CHECK(fscanf(f, "%f %f %f\n", &r, &g1, &b) == 3, "data");
                g2 = g1;
                break;
            case 4:
                CHECK(fscanf(f, "%f %f %f %f\n", &r, &g1, &g2, &b) == 4, "data");
                break;
            default:
                printf("components error\n");
                exit(1);
        }
        
        int this = i * (4096*8-1) / (length-1);
        
        Lut_R [this] = (int) round(r  * 4096 * 8);
        Lut_G1[this] = (int) round(g1 * 4096 * 8);
        Lut_G2[this] = (int) round(g2 * 4096 * 8);
        Lut_B [this] = (int) round(b  * 4096 * 8);
        
        int prev = (i-1) * (4096*8-1) / (length-1);;
        
        if (prev >= 0)
        {
            interp1(Lut_R  + prev, this - prev);
            interp1(Lut_G1 + prev, this - prev);
            interp1(Lut_G2 + prev, this - prev);
            interp1(Lut_B  + prev, this - prev);
        }

    }
    CHECK(fscanf(f, "}\n")                              == 0,   "}"      );

    fclose(f);
}

static void apply_lut(struct raw_info * raw_info, int16_t * raw16)
{
    int w = raw_info->width;
    int h = raw_info->height;

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x += 2)
        {
            /* Bayer pattern: [G2 B; R G1] */
            /* a and b can be either G2B (even lines) or RG1 (odd lines) */
            /* the LUTs already multiply the input values by 8 */
            int a = raw16[x   + y*w];
            int b = raw16[x+1 + y*w];
            a = (y % 2) ? Lut_R [a] : Lut_G2[a];
            b = (y % 2) ? Lut_G1[b] : Lut_B [b];
            raw16[x   + y*w] = MIN(a, 32760);
            raw16[x+1 + y*w] = MIN(b, 32760);
        }
    }
}

/* unpack raw data from 12-bit to 16-bit */
/* this also promotes the data to 15-bit,
 * so we can use int16_t for processing */
static void unpack12(struct raw_info * raw_info, int16_t * raw16)
{
    for (int y = 0; y < raw_info->height; y++)
    {
        for (int x = 0; x < raw_info->width; x += 2)
        {
            struct raw12_twopix * p = (struct raw12_twopix *)(raw_info->buffer + y * raw_info->pitch + x * sizeof(struct raw12_twopix) / 2);
            unsigned a = (p->a_hi << 4) | p->a_lo;
            unsigned b = (p->b_hi << 8) | p->b_lo;
            unsigned w = raw_info->width;
            raw16[x + y*w] = a << 3;
            raw16[x + 1 + y*w] = b << 3;
        }
    }
}

static void linear_fit(float* x, float* y, int n, float* a, float* b)
{
    /**
     * plain least squares
     * y = ax + b
     * a = (mean(xy) - mean(x)mean(y)) / (mean(x^2) - mean(x)^2)
     * b = mean(y) - a mean(x)
     */
    
    double mx = 0, my = 0, mxy = 0, mx2 = 0;
    for (int i = 0; i < n; i++)
    {
        mx += x[i];
        my += y[i];
        mxy += x[i] * y[i];
        mx2 += x[i] * x[i];
    }
    mx /= n;
    my /= n;
    mxy /= n;
    mx2 /= n;
    *a = (mxy - mx*my) / (mx2 - mx*mx);
    *b = my - (*a) * mx;
}

static struct
{
    int32_t * sum32;
    int16_t * min16;
    int16_t * max16;
    int size;
    int count;
    int gain;
    float exposures[1000];
    float averages[1000];
} A;

static void calc_avgframe_addframe(struct raw_info * raw_info, int16_t * raw16, int meta_gain, float meta_expo)
{
    int n = raw_info->width * raw_info->height;
    int new_frame_size = n * sizeof(A.sum32[0]);
    
    if (!A.sum32)
    {
        /* allocate memory on first call */
        A.size = new_frame_size;
        A.gain = meta_gain;
        A.sum32 = malloc(A.size);
        A.min16 = malloc(A.size/2);
        A.max16 = malloc(A.size/2);
        CHECK(A.sum32, "malloc");
        CHECK(A.max16, "malloc");
        CHECK(A.min16, "malloc");
        
        for (int i = 0; i < n; i++)
        {
            A.sum32[i] = 0;
            A.min16[i] = INT16_MAX;
            A.max16[i] = INT16_MIN;
        }
    }
    
    /* sanity checking */
    CHECK(A.size == new_frame_size, "all frames must have the same resolution.")
    CHECK(A.gain == meta_gain, "all frames must have the same gain setting.")
    CHECK(A.count < COUNT(A.exposures), "too many frames")

    /* find offset */
    int offsets[4];
    int avg_offset;
    calc_black_columns_offset(raw_info, raw16, offsets, &avg_offset);

    /* add current frame to accumulator */
    for (int i = 0; i < n; i++)
    {
        int p = (int) raw16[i] - avg_offset;
        A.sum32[i] += p;
        A.max16[i] = MAX(A.max16[i], p);
        A.min16[i] = MIN(A.min16[i], p);
    }

    /* compute average of current frame's active area */
    int w = raw_info->width;
    int h = raw_info->height;
    double avg = 0;
    for (int y = 0; y < h; y++)
    {
        for (int x = 8; x < w-8; x++)
        {
            int p = (int) raw16[x + y*w] - avg_offset;
            avg += p;
        }
    }
    avg /= h * (w-16);
    
    /* display values scaled back to 12-bit */
    printf("Average     : %.4f + %d\n", avg/8, avg_offset/8);
    
    /* record exposure and mean of each image */
    A.exposures[A.count] = meta_expo;
    A.averages [A.count] = avg/8;
    A.count++;
}

/* Output grayscale image to a 16-bit PGM file. */
static void save_pgm(char* filename, struct raw_info * raw_info, int32_t * raw32)
{
    printf("Writing %s...\n", filename);

    int w = raw_info->width;
    int h = raw_info->height;
    uint16_t* out = malloc(w * h * 2);

    for (int y = 0; y < h; y ++)
    {
        for (int x = 0; x < w; x ++)
        {
            int p = raw32[x + y*w];
            out[x + y*w] = ((p << 8) & 0xFF00) | ((p >> 8) & 0x00FF);
        }
    }

    FILE* f = fopen(filename, "wb");
    fprintf(f, "P5\n%d %d\n65535\n", w, h);

    fwrite(out, 1, w * h * 2, f);

    fclose(f);
    free(out);
}

#define CALC_DARK_FRAME 0
#define CALC_GAIN_FRAME 1
#define CALC_CLIP_FRAME 2

static void calc_avgframe_finish(char* out_filename, struct raw_info * raw_info, int type)
{
    CHECK(A.sum32, "invalid call to calc_avgframe_finish")
    
    int n = raw_info->width * raw_info->height;

    int offset = (type == CALC_DARK_FRAME) ? DARKFRAME_OFFSET :
                 (type == CALC_GAIN_FRAME) ? GAINFRAME_SCALING : 0 ;

    for (int i = 0; i < n; i++)
    {
        if (A.count > 4)
        {
            /* cheap way to get rid of some outliers: subtract min/max values before averaging */
            A.sum32[i] = (A.sum32[i] - A.min16[i] - A.max16[i] + A.count/2 - 1)
                         / (A.count - 2) + offset;
        }
        else
        {
            /* not enough frames for min/max subtraction */
            A.sum32[i] = (A.sum32[i] + A.count/2) / A.count + offset;
        }
    }

    float expo_min = 1e10;
    float expo_max = 0;
    for (int i = 0; i < A.count; i++)
    {
        expo_min = MIN(expo_min, A.exposures[i]);
        expo_max = MAX(expo_max, A.exposures[i]);
    }

    printf("\n");
    printf("-----------------------------\n");
    printf("\n");
    printf("Averaged %d frames exposed from %.2f to %.2f ms.\n", A.count, expo_min, expo_max);

    if (A.count < 4)
    {
        printf("You really need to average more frames (at least 16).\n");
    }
    else if (A.count < 16)
    {
        printf("Please consider averaging more frames (at least 16).\n");
    }

    /* for dark frames: compute dark current average, and adjust the dark frame to be a bias frame */
    if (type == CALC_DARK_FRAME)
    {
        float dark_current, dark_offset;
        linear_fit(A.exposures, A.averages, A.count, &dark_current, &dark_offset);
        
        if (!isfinite(dark_current))
        {
            printf("Could not compute dark current.\n");
            printf("Please use different exposures, e.g. from 1 to 50 ms.\n");
            dark_current = 0;
        }
        else
        {
            printf("Dark current: %.4f DN/ms\n", dark_current);
        }

        /* subtract average dark currents to get a "bias" frame */
        dark_offset = 0;
        for (int i = 0; i < A.count; i++)
        {
            dark_offset += A.exposures[i] * dark_current;
        }
        dark_offset /= A.count;
        
        int dark_off = (int)round(dark_offset);
        printf("Dark offset : %.2f\n", dark_off/8.0);
        for (int i = 0; i < n; i++)
        {
            A.sum32[i] -= dark_off;
        }
    }
    
    save_pgm(out_filename, raw_info, A.sum32);
    
    free(A.sum32);
    free(A.max16);
    free(A.min16);
    memset(&A, 0, sizeof(A));
}

static void calc_gainframe_do(struct raw_info * raw_info, int16_t * buf)
{
    /* we are going to fix the pattern noise,
     * then compute the "difference" (well, ratio)
     * and save it as a reference frame */
    int n = raw_info->width * raw_info->height;
    int frame_size = n * sizeof(buf[0]);
    int16_t * fixed = malloc(frame_size);
    CHECK(fixed, "malloc");
    
    memcpy(fixed, buf, frame_size);
    fix_pattern_noise(raw_info, fixed, 0, 0);

    /* note: gain is scaled by 16384 */
    int w = raw_info->width;
    for (int i = 0; i < n; i++)
    {
        int x = i % w;
        buf[i] = (x < 8 || x >= w-8)
               ? 16384                          /* do not touch black reference columns */
               : fixed[i] * 16384.0 / buf[i];   /* assume pattern noise in midtones is gain (PRNU) */
    }
    
    free(fixed);
}

/* linear (least squares) fit between images (see linear_fit) */
static struct
{
    double * my;
    double * mxy;
    double mx;
    double mx2;
    int size;
    int count;
    int gain;
    float expo_min;
    float expo_max;
} L;

static void calc_linfitframes_addframe(struct raw_info * raw_info, int16_t * raw16, int meta_gain, float meta_expo)
{
    int n = raw_info->width * raw_info->height;
    int new_frame_size = n * sizeof(L.my[0]);
    
    if (!L.my)
    {
        /* allocate memory on first call */
        L.size = new_frame_size;
        L.gain = meta_gain;
        L.my = malloc(L.size);
        L.mxy = malloc(L.size);
        CHECK(L.my,  "malloc");
        CHECK(L.mxy, "malloc");
        
        for (int i = 0; i < n; i++)
        {
            L.my[i]  = 0;
            L.mxy[i] = 0;
        }
        
        L.expo_min = 1e10;
        L.expo_max = 0;
    }
    
    /* sanity checking */
    CHECK(L.size == new_frame_size, "all frames must have the same resolution.")
    CHECK(L.gain == meta_gain, "all frames must have the same gain setting.")

    /* find offset */
    int offsets[4];
    int avg_offset;
    calc_black_columns_offset(raw_info, raw16, offsets, &avg_offset);

    /* add current frame to accumulators */
    L.mx += meta_expo;
    L.mx2 += meta_expo * meta_expo;
    for (int i = 0; i < n; i++)
    {
        int p = (int) raw16[i] - avg_offset;
        L.my[i]  += p;
        L.mxy[i] += meta_expo * p;
    }
    L.count++;
    
    /* keep track of min/max exposure, for printing at the end */
    L.expo_max = MAX(L.expo_max, meta_expo);
    L.expo_min = MIN(L.expo_min, meta_expo);
}

static void calc_linfitframes_finish(char* offset_filename, char* gain_filename, struct raw_info * raw_info)
{
    CHECK(L.my, "invalid call to calc_linfitframes_finish")
    
    int n = raw_info->width * raw_info->height;

    printf("\n");
    printf("-----------------------------\n");
    printf("\n");
    printf("Combined %d frames exposed from %.2f to %.2f ms.\n", L.count, L.expo_min, L.expo_max);

    if (L.count < 16)
    {
        printf("You really need to use more frames (at least 64).\n");
    }
    else if (L.count < 64)
    {
        printf("Please consider using more frames (at least 64).\n");
    }
    
    /* finish the linear fitting */
    L.mx /= L.count;
    L.mx2 /= L.count;
    for (int i = 0; i < n; i++)
    {
        L.my[i]  /= L.count;
        L.mxy[i] /= L.count;
    }
    
    int32_t * a = malloc(n * sizeof(a[0]));
    int32_t * b = malloc(n * sizeof(a[0]));

    for (int i = 0; i < n; i++)
    {
        /* note: when scaling, we keep in mind the raw data was multiplied by 8
         * when it was promoted to raw16, so we scale it back to 12-bit values */
        double aa = (L.mxy[i] - L.mx * L.my[i]) / (L.mx2 - L.mx * L.mx);
        a[i] = (int)round(aa * DCNUFRAME_SCALING / 8 + DCNUFRAME_OFFSET);
        b[i] = (int)round((L.my[i] - aa * L.mx) + DARKFRAME_OFFSET);
    }

    save_pgm(offset_filename, raw_info, b);
    save_pgm(gain_filename,   raw_info, a);
    
    free(L.my);
    free(L.mxy);
    free(a);
    free(b);
    memset(&L, 0, sizeof(L));
}

/* pack raw data from 16-bit to 12-bit */
/* this also adds some anti-posterization noise,
 * which acts somewhat like introducing one extra bit of detail */
static void pack12(struct raw_info * raw_info, int16_t * buf)
{
    for (int y = 0; y < raw_info->height; y++)
    {
        for (int x = 0; x < raw_info->width; x += 2)
        {
            struct raw12_twopix * p = (struct raw12_twopix *)(raw_info->buffer + y * raw_info->pitch + x * sizeof(struct raw12_twopix) / 2);
            unsigned w = raw_info->width;
            unsigned a = ((MAX(buf[x + y*w],    0) >> 2) + rand()%2) >> 1;
            unsigned b = ((MAX(buf[x + 1 + y*w],0) >> 2) + rand()%2) >> 1;
            p->a_lo = a; p->a_hi = a >> 4;
            p->b_lo = b; p->b_hi = b >> 8;
        }
    }
}

/* todo: move this into FPGA */
void reverse_lines_order(char* buf, int count, int width)
{
    /* line width, in bytes */
    int pitch = width * 12 / 8;
    void* aux = malloc(pitch);
    CHECK(aux, "malloc");
    
    /* swap odd and even lines */
    int i;
    int height = count / pitch;
    for (i = 0; i < height; i += 2)
    {
        memcpy(aux, buf + i * pitch, pitch);
        memcpy(buf + i * pitch, buf + (i+1) * pitch, pitch);
        memcpy(buf + (i+1) * pitch, aux, pitch);
    }
    
    free(aux);
}

static void hdmi_reorder(struct raw_info * raw_info)
{
    void* aux = malloc(raw_info->frame_size);
    CHECK(aux, "malloc");
    memcpy(aux, raw_info->buffer, raw_info->frame_size);

    /**
     * We have a 3840x2160 Bayer image split into 4 1920x1080 sub-images, interleaved:
     * - raw buffer is 7680x1080
     * - raw(:, 1:4:end) is red
     * - raw(:, 2:4:end) is green1
     * - raw(:, 3:4:end) is green2
     * - raw(:, 4:4:end) is blue
     *
     * So, to re-arrange it, we will do the following mapping:
     *
     *   source    destination
     * (RGGBRGGB) (Bayer RG;GB)
     *     X Y      x y
     * R: (0,0) -> (0,0)
     * G: (1,0) -> (1,0)
     * G: (2,0) -> (0,1)
     * B: (3,0) -> (1,1)
     * ...
     * 
     */
    
    /* handle one Bayer block at a time */
    for (int y = 0; y < raw_info->height-2; y += 2)
    {
        for (int x = 0; x < raw_info->width; x += 2)
        {
            /* pixel position in source image */
            int i = (x/2 + y/2 * raw_info->width/2) * 4;
            
            /* src1 is RG, src2 is GB from source image (both on the same line) */
            struct raw12_twopix * src1 = (struct raw12_twopix *)(aux + i * sizeof(struct raw12_twopix) / 2);
            struct raw12_twopix * src2 = src1 + 1;
            
            /* dst1 is RG, dst2 is GB from destination image (GB under RG) */
            /* note: we offset y by 1 because the rest of the code assumes [GB;RG] order */
            struct raw12_twopix * dst1 = (struct raw12_twopix *)(raw_info->buffer + (y+1) * raw_info->pitch + x * sizeof(struct raw12_twopix) / 2);
            struct raw12_twopix * dst2 = dst1 + raw_info->pitch / sizeof(struct raw12_twopix);

            *dst1 = *src1;
            *dst2 = *src2;
        }
    }
    
    free(aux);
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

int main(int argc, char** argv)
{
    if (argc == 1)
    {
        printf("DNG converter for Apertus .raw12 files\n");
        printf("\n");
        printf("Usage:\n");
        printf("  %s input.raw12 [input2.raw12] [options]\n", argv[0]);
        printf("  cat input.raw12 | %s output.dng [options]\n", argv[0]);
        printf("\n");
        printf("Flat field correction:\n");
        printf(" - for each gain (N=1,2,3,4), you may use the following reference images:\n");
        printf(" - darkframe-xN.pgm will be subtracted (data is x8 + 1024)\n");
        printf(" - dcnuframe-xN.pgm will be multiplied by exposure and subtracted (x8192 + 8192)\n");
        printf(" - gainframe-xN.pgm will be multiplied (1.0 = 16384)\n");
        printf(" - clipframe-xN.pgm will be subtracted from highlights (x8)\n");
        printf(" - reference images are 16-bit PGM, in the current directory\n");
        printf(" - they are optional, but gain/clip frames require a dark frame\n");
        printf(" - black ref columns will also be subtracted if you use a dark frame.\n");
        printf("\n");
        printf("Creating reference images:\n");
        printf(" - dark frames: average as many as practical, for each gain setting,\n");
        printf("   with exposures ranging from around 1ms to 50ms:\n");
        printf("        raw2dng --calc-darkframe *-gainx1-*.raw12 \n");
        printf(" - DCNU (dark current nonuniformity) frames: similar to dark frames,\n");
        printf("   just take a lot more images to get a good fit (use 256 as a starting point):\n");
        printf("        raw2dng --calc-dcnuframe *-gainx1-*.raw12 \n");
        printf("   (note: the above will compute BOTH a dark frame and a dark current frame)\n");
        printf(" - gain frames: average as many as practical, for each gain setting,\n");
        printf("   with a normally exposed blank OOF wall as target, or without lens\n");
        printf("   (currently used for pattern noise reduction only):\n");
        printf("        raw2dng --calc-gainframe *-gainx1-*.raw12 \n");
        printf(" - clip frames: average as many as practical, for each gain setting,\n");
        printf("   with a REALLY overexposed blank out-of-focus wall as target:\n");
        printf("        raw2dng --calc-clipframe *-gainx1-*.raw12 \n");
        printf(" - Always compute these frames in the order listed here\n");
        printf("   (dark/dcnu frames, then gain frames (optional), then clip frames (optional).\n");
        
        printf("\n");
        show_commandline_help(argv[0]);
        return 0;
    }

    /* parse all command-line options */
    for (int k = 1; k < argc; k++)
        if (argv[k][0] == '-')
            parse_commandline_option(argv[k]);
    show_active_options();

    char dark_filename[20];
    char dcnu_filename[20];
    char gain_filename[20];
    char clip_filename[20];
    char lut_filename[20];

    /* all other arguments are input or output files */
    for (int k = 1; k < argc; k++)
    {
        if (argv[k][0] == '-')
            continue;
        
        FILE* fi;
        char* out_filename;

        printf("\n%s\n", argv[k]);
        
        if (endswith(argv[k], ".raw12"))
        {
            fi = fopen(argv[k], "rb");
            CHECK(fi, "could not open %s", argv[k]);

            /* replace input file extension with .DNG */
            static char fo[256];
            snprintf(fo, sizeof(fo), "%s", argv[k]);
            char* ext = strrchr(fo, '.');
            if (!ext) ext = fo + strlen(fo) - 4;
            ext[0] = '.';
            ext[1] = 'D';
            ext[2] = 'N';
            ext[3] = 'G';
            ext[4] = '\0';
            out_filename = fo;
        }
        else if (endswith(argv[k], ".dng") || endswith(argv[k], ".DNG"))
        {
            fi = stdin;
            out_filename = argv[k];
            if (!image_height) image_height = 3072;
        }
        else
        {
            printf("Unknown file type.\n");
            continue;
        }
        
        int width = image_width ? image_width : hdmi_ramdump ? 1920*2 : 4096;
        int height = image_height;
        int has_metadata = 0;
        if (!height)
        {
            /* autodetect height from file size, if not specified in the command line */
            fseek(fi, 0, SEEK_END);
            height = ftell(fi) / (width * 12 / 8);
            has_metadata = (ftell(fi) - (width * height * 12 / 8) == 256);
            fseek(fi, 0, SEEK_SET);
        }
        raw_set_geometry(width, height, 0, 0, 0, 0);
        
        /* print current settings */
        printf("Resolution  : %d x %d\n", raw_info.width, raw_info.height);
        printf("Frame size  : %d bytes\n", raw_info.frame_size);
        switch(raw_info.cfa_pattern) {
            case 0x02010100:
                printf("Bayer Order : RGGB \n");    
                break;
            case 0x01000201:
                printf("Bayer Order : GBRG \n");    
                break;
            case 0x01020001:
                printf("Bayer Order : GRBG \n");    
                break;
            case 0x00010102:
                printf("Bayer Order : BGGR \n");    
                break;
        }


        /* load the raw data and convert it to DNG */
        char* raw = malloc(raw_info.frame_size);
        CHECK(raw, "malloc");
        
        int r = fread(raw, 1, raw_info.frame_size, fi);
        CHECK(r == raw_info.frame_size, "fread");
        raw_info.buffer = raw;

        metadata_clear();

        int meta_gain = 0;
        float meta_expo = 0;
        int meta_ystart = 0;

        if (has_metadata)
        {
            uint16_t registers[128];
            int r = fread(registers, 1, 256, fi);
            CHECK(r == 256, "fread");
            metadata_extract(registers);
            
            meta_gain = metadata_get_gain(registers);
            meta_expo = metadata_get_exposure(registers);
            meta_ystart = metadata_get_ystart(registers);
            
            if (dump_regs)
            {
                /* dump registers and skip the output file */
                metadata_dump_registers(registers);
                goto cleanup;
            }
        }
        else if (dump_regs)
        {
            printf("Metadata block not found.\n");
            goto cleanup;
        }

        if (hdmi_ramdump && black_level == 0xFFFF)
        {
            /* in the HDMI experiment, there were no black reference columns enabled */
            black_level = 0;
        }

        /* use black and white levels from command-line */
        raw_info.black_level = black_level;
        raw_info.white_level = white_level;
        
        printf("Black level : %d\n", raw_info.black_level);
        printf("White level : %d\n", raw_info.white_level);
        
        if (raw_info.black_level < 0)
        {
            /* We can't use a negative black level,
             * but we may want to use one to fix green color cast in some images.
             * Workaround: add a constant offset to the raw data, and use black=0 in exif.
             */
            int offset = -raw_info.black_level;     /* positive number */
            printf("Raw offset  : %d\n", offset);
            raw12_data_offset(raw_info.buffer, raw_info.frame_size, offset);
            raw_info.black_level = 0;
            raw_info.white_level = MIN(raw_info.white_level + offset, 4095);
        }

        if (hdmi_ramdump)
        {
            printf("HDMI reorder...\n");
            hdmi_reorder(&raw_info);
        }

        if (swap_lines)
        {
            printf("Line swap...\n");
            reverse_lines_order(raw_info.buffer, raw_info.frame_size, raw_info.width);
        }
        
        if (no_processing)
        {
            /* skip all processing (except reordering) */
            goto save_output;
        }
        
        int16_t * raw16 = 0;

        snprintf(dark_filename, sizeof(dark_filename), "darkframe-x%d.pgm", meta_gain);
        snprintf(dcnu_filename, sizeof(dcnu_filename), "dcnuframe-x%d.pgm", meta_gain);
        snprintf(gain_filename, sizeof(gain_filename), "gainframe-x%d.pgm", meta_gain);
        snprintf(clip_filename, sizeof(clip_filename), "clipframe-x%d.pgm", meta_gain);
        snprintf(lut_filename,  sizeof(lut_filename),  "lut-x%d.spi1d",     meta_gain);

        /* note: gain frame, clip frame and black column subtraction
         * are only enabled if we also use a dark frame */
        int use_darkframe = !calc_dcnuframe && !calc_darkframe &&
                            !no_darkframe && meta_gain && file_exists_warn(dark_filename);

        int use_dcnuframe = !calc_dcnuframe && !calc_darkframe && use_darkframe &&
                            !no_dcnuframe && meta_gain && file_exists_warn(dcnu_filename);
        
        int use_gainframe = !calc_gainframe && !calc_dcnuframe && !calc_darkframe && use_darkframe &&
                            !no_gainframe && meta_gain && file_exists_warn(gain_filename);
        
        int use_clipframe = !calc_clipframe && !calc_gainframe && !calc_dcnuframe && !calc_darkframe && use_darkframe &&
                            !no_clipframe && meta_gain && file_exists_warn(clip_filename);
        
        use_lut = use_lut && file_exists_warn(lut_filename);
        
        if (!use_darkframe && !calc_darkframe && !calc_dcnuframe)
        {
            no_blackcol = 1;
        }

        int raw16_postprocessing =
            (calc_darkframe || calc_dcnuframe || calc_gainframe || calc_clipframe ||
             use_darkframe  || use_gainframe  || use_clipframe  ||
             use_lut || fixpn);
        
        if (raw16_postprocessing)
        {
            /* if we process the raw data, unpack it to int16_t (easier to work with) */
            /* this also multiplies the values by 8 */
            raw16 = malloc(raw_info.width * raw_info.height * sizeof(raw16[0]));
            unpack12(&raw_info, raw16);
        }
        
        if (use_darkframe)
        {
            printf("Dark frame  : %s\n", dark_filename);
            int16_t * dark = malloc(raw_info.width * raw_info.height * sizeof(dark[0]));
            int16_t * dcnu = 0;
            int extra_offset = 0;

            read_reference_frame(dark_filename, dark, &raw_info, meta_ystart);
            
            if (use_dcnuframe)
            {
                printf("DCNU frame  : %s\n", dcnu_filename);
                dcnu = malloc(raw_info.width * raw_info.height * sizeof(dcnu[0]));
                read_reference_frame(dcnu_filename, dcnu, &raw_info, meta_ystart);
            }
            else
            {
                int dark_current = (int) roundf(dark_current_avg * meta_gain * meta_expo * 8);
                printf("Dark current: %d\n", dark_current/8);
                extra_offset = dark_current;
            }

            subtract_dark_frame(&raw_info, raw16, dark, extra_offset, dcnu, meta_expo);
            free(dark);
            if (dcnu) free(dcnu);
        }

        if (!no_blackcol)
        {
            subtract_black_columns(&raw_info, raw16);
        }

        if (use_gainframe)
        {
            printf("Gain frame  : %s\n", gain_filename);
            uint16_t * gain = malloc(raw_info.width * raw_info.height * sizeof(gain[0]));
            read_reference_frame(gain_filename, (int16_t*) gain, &raw_info, meta_ystart);
            apply_gain_frame(&raw_info, raw16, gain);
            free(gain);
        }

        if (use_clipframe)
        {
            /* note: when computing the clip frame, you should also apply dark and gain frames to it */
            printf("Clip frame  : %s\n", clip_filename);
            uint16_t * clip = malloc(raw_info.width * raw_info.height * sizeof(clip[0]));
            read_reference_frame(clip_filename, (int16_t*) clip, &raw_info, meta_ystart);
            apply_clip_frame(&raw_info, raw16, clip);
            free(clip);
        }
        
        if (fixpn)
        {
            int fixpn_flags = fixpn_flags1 | fixpn_flags2;
            fix_pattern_noise(&raw_info, raw16, fixpn == 1, fixpn_flags);
        }

        if (use_lut)
        {
            /* no newline here (read_lut will print more info) */
            printf("LUT file    : %s ", lut_filename);
            read_lut(lut_filename);
            apply_lut(&raw_info, raw16);
        }

        if (calc_darkframe || calc_dcnuframe || calc_gainframe || calc_clipframe)
        {
            if (meta_ystart || raw_info.height != 3072)
            {
                printf("Error: calibration frames must be full-resolution.\n");
                exit(1);
            }
            
            if ((calc_gainframe || calc_clipframe) && !use_darkframe)
            {
                printf("Error: gain and clip frames require a dark frame.\n");
                exit(1);
            }
            
            if (calc_gainframe)
            {
                /* estimate gain from each frame, then average those estimations */
                calc_gainframe_do(&raw_info, raw16);
            }
            
            if (calc_dcnuframe)
            {
                /* linear fit for multiple frames */
                calc_linfitframes_addframe(&raw_info, raw16, meta_gain, meta_expo);
            }
            else
            {
                /* generic averaging routine */
                calc_avgframe_addframe(&raw_info, raw16, meta_gain, meta_expo);
            }
            
            /* no need to repack to 12 bits */
            free(raw16); raw16 = 0;
            goto cleanup;
        }

        if (raw16_postprocessing)
        {
            /* processing done, repack the 16-bit data into 12-bit raw buffer */
            pack12(&raw_info, raw16);
            free(raw16); raw16 = 0;
        }

save_output:
        /* save the DNG */
        printf("Output file : %s\n", out_filename);
        save_dng(out_filename, &raw_info);

cleanup:
        fclose(fi);
        free(raw); raw_info.buffer = 0;
    }
    
    if (calc_darkframe)
    {
        calc_avgframe_finish(dark_filename, &raw_info, CALC_DARK_FRAME);
    }
    else if (calc_dcnuframe)
    {
        calc_linfitframes_finish(dark_filename, dcnu_filename, &raw_info);
    }
    else if (calc_gainframe)
    {
        calc_avgframe_finish(gain_filename, &raw_info, CALC_GAIN_FRAME);
    }
    else if (calc_clipframe)
    {
        calc_avgframe_finish(clip_filename, &raw_info, CALC_CLIP_FRAME);
    }

    printf("Done.\n\n");
    
    return 0;
}

int raw_get_pixel(int x, int y)
{
    /* fixme: return valid values here to create a thumbnail */
    return 0;
}
