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
#include "math.h"
#include "raw.h"
#include "chdk-dng.h"
#include "cmdoptions.h"
#include "patternnoise.h"

int black_level = 0;
int white_level = 4095;
int image_height = 0;
int swap_lines = 0;
int fixpn = 0;
int fixpn_flags1 = 0;
int fixpn_flags2 = 0;

struct cmd_group options[] = {
    {
        "Options", (struct cmd_option[]) {
            { &black_level,    1, "--black=%d",    "Set black level (default 0)\n"
                             "                      - negative values allowed" },
            { &white_level,    1, "--white=%d",    "Set white level (default 4095)\n"
                             "                      - if too high, you may get pink highlights\n"
                             "                      - if too low, useful highlights may clip to white" },
            { &image_height,   1, "--height=%d",    "Set image height\n"
                             "                      - default: autodetect from file size\n"
                             "                      - if input is stdin, default is 3072" },
            { &swap_lines,     1,  "--swap-lines",  "Swap lines in the raw data\n"
                              "                      - workaround for an old Beta bug" },
            { &fixpn,          1,  "--fixpn",           "Fix pattern noise (slow)" },
            OPTION_EOL,
        },
    },
    {
        "Debug options", (struct cmd_option[]) {
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
#define CHECK(ok, fmt,...) { if (!ok) FAIL(fmt, ## __VA_ARGS__); }

#define MIN(a,b) \
   ({ __typeof__ ((a)+(b)) _a = (a); \
      __typeof__ ((a)+(b)) _b = (b); \
     _a < _b ? _a : _b; })

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

/* computed by matching CC3.raw12 with _GEZ6498.NEF (Nikon D800E) */
#define CAM_COLORMATRIX1                          \
   12608, 10000,    -4345, 10000,    -962, 10000, \
   -2849, 10000,    10361, 10000,    1958, 10000, \
    -572, 10000,     2786, 10000,    6293, 10000

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
    int i;
    struct raw12_twopix * buf2 = (struct raw12_twopix *) buf;
    for (i = 0; i < frame_size / sizeof(struct raw12_twopix); i ++)
    {
        unsigned a = (buf2[i].a_hi << 4) | buf2[i].a_lo;
        unsigned b = (buf2[i].b_hi << 8) | buf2[i].b_lo;
        
        a = MIN(a + offset, 4095);
        b = MIN(b + offset, 4095);
        
        buf2[i].a_lo = a; buf2[i].a_hi = a >> 4;
        buf2[i].b_lo = b; buf2[i].b_hi = b >> 8;
    }
}

/* todo: move this into FPGA */
void reverse_lines_order(char* buf, int count)
{
    /* 4096 pixels per line */
    struct line
    {
        struct raw12_twopix line[2048];
    } __attribute__((packed));
    
    /* swap odd and even lines */
    int i;
    int height = count / sizeof(struct line);
    struct line * bufl = (struct line *) buf;
    for (i = 0; i < height; i += 2)
    {
        struct line aux;
        aux = bufl[i];
        bufl[i] = bufl[i+1];
        bufl[i+1] = aux;
    }
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
        show_commandline_help(argv[0]);
        return 0;
    }

    /* parse all command-line options */
    for (int k = 1; k < argc; k++)
        if (argv[k][0] == '-')
            parse_commandline_option(argv[k]);
    show_active_options();

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
            char* ext = strchr(fo, '.');
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
            continue;
        }
        
        int width = 4096;
        int height = image_height;
        if (!height)
        {
            /* there are 4096 columns in a .raw12 file, but the number of lines is variable */
            /* autodetect it from file size, if not specified in the command line */
            fseek(fi, 0, SEEK_END);
            height = ftell(fi) / (width * 12 / 8);
            fseek(fi, 0, SEEK_SET);
        }
        raw_set_geometry(width, height, 0, 0, 0, 0);
        
        /* use black and white levels from command-line */
        raw_info.black_level = black_level;
        raw_info.white_level = white_level;
        
        /* print current settings */
        printf("Resolution  : %d x %d\n", raw_info.width, raw_info.height);
        printf("Frame size  : %d bytes\n", raw_info.frame_size);
        printf("Black level : %d\n", raw_info.black_level);
        printf("White level : %d\n", raw_info.white_level);
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

        if (swap_lines)
        {
            printf("Line swap...\n");
            reverse_lines_order(raw_info.buffer, raw_info.frame_size);
        }
        
        if (fixpn)
        {
            int fixpn_flags = fixpn_flags1 | fixpn_flags2;
            fix_pattern_noise(&raw_info, fixpn_flags);
        }
        
        /* save the DNG */
        printf("Output file : %s\n", out_filename);
        save_dng(out_filename, &raw_info);
        fclose(fi);
        
        free(raw); raw_info.buffer = 0;
    }

    printf("Done.\n");
    
    return 0;
}

int raw_get_pixel(int x, int y)
{
    /* fixme: return valid values here to create a thumbnail */
    return 0;
}
