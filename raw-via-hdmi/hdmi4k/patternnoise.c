/*
 * Pattern noise correction
 * Copyright (C) 2015 A1ex
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

#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "wirth.h"
#include "math.h"
#include "patternnoise.h"

static int g_debug_flags;

#define MIN(a,b) \
   ({ typeof ((a)+(b)) _a = (a); \
      typeof ((a)+(b)) _b = (b); \
     _a < _b ? _a : _b; })

#define MAX(a,b) \
   ({ typeof ((a)+(b)) _a = (a); \
       typeof ((a)+(b)) _b = (b); \
     _a > _b ? _a : _b; })

#define ABS(a) \
   ({ __typeof__ (a) _a = (a); \
     _a > 0 ? _a : -_a; })

#define COERCE(x,lo,hi) MAX(MIN((x),(hi)),(lo))
#define COUNT(x)        ((int)(sizeof(x)/sizeof((x)[0])))

/* out = a - b */
static void subtract(int16_t * a, int16_t * b, int16_t * out, int w, int h)
{
    for (int i = 0; i < w*h; i++)
    {
        out[i] = a[i] - b[i];
    }
}

/* operate on RGB PPM data */
/* w and h are the size of input buffer; the output buffer will have the dimensions swapped */
static void transpose(uint16_t * in, uint16_t * out, int w, int h)
{
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            for (int ch = 0; ch < 3; ch++)
            {
                out[3*y+ch + x*h*3] = in[3*x+ch + y*w*3];
            }
        }
    }
}

static void horizontal_gradient(int16_t * in, int16_t * out, int w, int h)
{
    for (int i = 2; i < w*h-2; i++)
    {
        out[i] = in[i-2] - in[i+2];
    }
    
    out[0] = out[1] = out[w*h-1] = out[w*h-2] = 0;
}

static void horizontal_edge_aware_blur_rgb(
    int16_t * in_r,  int16_t * in_g,  int16_t * in_b,
    int16_t * out_r, int16_t * out_g, int16_t * out_b,
    int w, int h, int edge_thr, int strength)
{
    const int NMAX = 256;
    int g[NMAX];
    int rg[NMAX];
    int bg[NMAX];
    if (strength > NMAX)
    {
        fprintf(stderr, "FIXME: blur too strong\n");
        return;
    }
    
    strength /= 2;

    /* precompute red-green and blue-green */
    int16_t * dif_rg = malloc(w * h * sizeof(dif_rg[0]));
    int16_t * dif_bg = malloc(w * h * sizeof(dif_bg[0]));
    subtract(in_r, in_g, dif_rg, w, h);
    subtract(in_b, in_g, dif_bg, w, h);

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            int p0 = in_g[x + y*w];
            int num = 0;

            /* range of pixels similar to p0 */
            /* it will contain at least 1 pixel, and at most from 2*strength + 1 pixels */
            int xl = x-1;
            int xr = x+1;

            /* go to the right, until crossing the threshold */
            while (xr < MIN(x + strength, w))
            {
                int p = in_g[xr + y*w];
                if (abs(p - p0) > edge_thr)
                    break;
                xr++;
            }

            /* same, to the left */
            while (xl >= MAX(x - strength, 0))
            {
                int p = in_g[xl + y*w];
                if (abs(p - p0) > edge_thr)
                    break;
                xl--;
            }
            
            /* now take the medians from this interval */
            for (int xx = xl+1; xx < xr; xx++)
            {
                g[num]  = in_g[xx + y*w];
                rg[num] = dif_rg[xx + y*w];
                bg[num] = dif_bg[xx + y*w];
                num++;
            }
            
            //~ fprintf(stderr, "%d ", num);
            int mg = median_int_wirth(g, num);
            out_g[x + y*w] = mg;
            out_r[x + y*w] = median_int_wirth(rg, num) + mg;
            out_b[x + y*w] = median_int_wirth(bg, num) + mg;
        }
    }
    
    free(dif_rg);
    free(dif_bg);
}

/* Find and apply a scalar offset to each column, to reduce pattern noise */
/* original: input and output */
/* denoised: input only */
void fix_column_noise(int16_t * original, int16_t * denoised, int w, int h)
{
    /* let's say the difference between original and denoised is mostly noise */
    int16_t * noise = malloc(w * h * sizeof(noise[0]));
    subtract(original, denoised, noise, w, h);

    /* from this noise, keep the FPN part (constant offset for each line/column) */
    int col_offsets_size = w * sizeof(int);
    int* col_offsets = malloc(col_offsets_size);
    int* noise_row = malloc(MAX(w,h) * sizeof(noise_row[0]));
    int  noise_row_num = 0;

    /* certain areas will give false readings, mask them out */
    int16_t * mask  = malloc(w * h * sizeof(mask[0]));
    int16_t * hgrad = malloc(w * h * sizeof(mask[0]));

    horizontal_gradient(original, hgrad, w, h);

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            int pixel = original[x + y*w];
            int hgradient = ABS(hgrad[x + y*w]);
            int noise_val = noise[x + y*w];

            mask[x + y*w] =
                (hgradient > 5000) ||       /* mask out pixels on a strong edge, that is clearly not pattern noise */
                (pixel > 20000) ||          /* mask out very bright pixels */
                (ABS(noise_val) > 2000) ||  /* mask out values that are clearly not noise */
                noise_val == 0;             /* hack: figure out why does this appear to give much better results, and whether there are side effects */
        }
    }

    if (g_debug_flags & FIXPN_DBG_DENOISED)
    {
        /* debug: show denoised image */
        for (int i = 0; i < w*h; i++)
            original[i] = MAX(denoised[i], 0);
        goto end;
    }
    else if (g_debug_flags & FIXPN_DBG_NOISE)
    {
        /* debug: show the noise image */
        for (int i = 0; i < w*h; i++)
        {
            if (mask[i]) noise[i] = -1500;
            original[i] = noise[i] + 1500;
        }
        goto end;
    }
    else if (g_debug_flags & FIXPN_DBG_MASK)
    {
        /* debug: show the mask */
        for (int i = 0; i < w*h; i++)
            original[i] = mask[i] * 10000;
        goto end;
    }

    /* take the median value for each column, in the noise image */
    for (int x = 0; x < w; x++)
    {
        noise_row_num = 0;
        for (int y = 0; y < h; y++)
        {
            if (mask[x + y*w] == 0)
            {
                noise_row[noise_row_num++] = noise[x + y*w];
            }
        }
        
        int offset = (noise_row_num < 10) ? 0 : -median_int_wirth(noise_row, noise_row_num);

        col_offsets[x] = offset;
    }

    /* remove median from offsets, to prevent color cast */
    /* note: median modifies the array, so we allocate a copy */
    int* col_offsets_copy = malloc(col_offsets_size);
    memcpy(col_offsets_copy, col_offsets, col_offsets_size);
    int mc = median_int_wirth(col_offsets_copy, w);
    free(col_offsets_copy);
    
    /* almost done, now apply the offsets */
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            original[x + y*w] = COERCE((int)original[x + y*w] + col_offsets[x] - mc, 0, 32760);
        }
    }

end:
    free(noise);
    free(col_offsets);
    free(noise_row);
    free(mask);
    free(hgrad);
}

/* extract a color channel from a RGB image (PPM order) */
/* in is w x h x 3, out is w x h signed, ch is 0..2 */
void extract_channel(uint16_t * in, int16_t * out, int w, int h, int ch)
{
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            out[x + y*w] = in[x*3+ch + y*w*3] / 2;
        }
    }
}

/* set a color channel into a RGB image (PPM order) */
/* out is w x h x 3, in is w x h signed, ch is 0..2 */
static void set_channel(uint16_t * out, int16_t * in, int w, int h, int ch)
{
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            out[x*3+ch + y*w*3] = in[x + y*w] * 2;
        }
    }
}

/* denoised is optional */
static void fix_column_noise_rgb(uint16_t * rgb, uint16_t * denoised, int w, int h)
{
    int16_t * r  = malloc(w * h * sizeof(r[0]));
    int16_t * g  = malloc(w * h * sizeof(r[0]));
    int16_t * b  = malloc(w * h * sizeof(r[0]));
    int16_t * rs = malloc(w * h * sizeof(r[0]));   /* r after smoothing */
    int16_t * gs = malloc(w * h * sizeof(r[0]));   /* g after smoothing */
    int16_t * bs = malloc(w * h * sizeof(r[0]));   /* b after smoothing */
    
    /* extract color channels from RGB data */
    extract_channel(rgb, r, w, h, 0);
    extract_channel(rgb, g, w, h, 1);
    extract_channel(rgb, b, w, h, 2);
    
    if (denoised)
    {
        extract_channel(denoised, rs, w, h, 0);
        extract_channel(denoised, gs, w, h, 1);
        extract_channel(denoised, bs, w, h, 2);
    }
    else
    {
        /* strong horizontal denoising (1-D median blur on G, R-G and B-G, stop on edge */
        /* (this step takes a lot of time) */
        horizontal_edge_aware_blur_rgb(r, g, b, rs, gs, bs, w, h, 5000, 50);
        fprintf(stderr, "."); fflush(stdout);
    }

    /* after blurring horizontally, the difference reveals vertical FPN */
    
    fix_column_noise(r, rs, w, h);
    fix_column_noise(g, gs, w, h);
    fix_column_noise(b, bs, w, h);
    fprintf(stderr, "."); fflush(stdout);

    /* commit changes */
    set_channel(rgb, r, w, h, 0);
    set_channel(rgb, g, w, h, 1);
    set_channel(rgb, b, w, h, 2);

    /* cleanup */
    free(r);
    free(g);
    free(b);
    free(rs);
    free(gs);
    free(bs);
}

void fix_pattern_noise_ex(uint16_t * rgb, uint16_t * denoised, int width, int height, int row_noise_only, int debug_flags)
{
    fprintf(stderr, "Fixing %s noise", row_noise_only ? "row" : "pattern");
    fflush(stdout);
    
    g_debug_flags = debug_flags;
    
    int w = width;
    int h = height;
    
    /* fix vertical noise, then transpose and repeat for the horizontal one */
    /* not very efficient, but at least avoids duplicate code */
    /* note: when debugging, we process only one direction */
    if (!row_noise_only && (!g_debug_flags || (g_debug_flags & FIXPN_DBG_COLNOISE)))
    {
        fix_column_noise_rgb(rgb, denoised, w, h);
    }
    
    if (row_noise_only || !g_debug_flags || !(g_debug_flags & FIXPN_DBG_COLNOISE))
    {
        /* transpose, process just like before, then transpose back */
        int size = w * h * 3 * sizeof(rgb[0]);
        uint16_t * rgb_t = malloc(size);
        uint16_t * denoised_t = denoised ? malloc(size) : 0;
        transpose(rgb, rgb_t, w, h);
        if (denoised_t) transpose(denoised, denoised_t, w, h);
        fix_column_noise_rgb(rgb_t, denoised_t, h, w);
        transpose(rgb_t, rgb, h, w);
        free(rgb_t);
        if (denoised_t) free(denoised_t);
    }
    
    fprintf(stderr, "\n");
}


void fix_pattern_noise(uint16_t * rgb, int width, int height, int row_noise_only, int debug_flags)
{
    fix_pattern_noise_ex(rgb, 0, width, height, row_noise_only, debug_flags);
}
