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
#include "omp.h"

static int g_debug_flags;

#define MIN(a,b) \
   ({ typeof ((a)+(b)) _a = (a); \
      typeof ((a)+(b)) _b = (b); \
     _a < _b ? _a : _b; })

#define MAX(a,b) \
   ({ typeof ((a)+(b)) _a = (a); \
       typeof ((a)+(b)) _b = (b); \
     _a > _b ? _a : _b; })

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

/* out = (a + b) / 2 */
static void average(int16_t * a, int16_t * b, int16_t * out, int w, int h)
{
    for (int i = 0; i < w*h; i++)
    {
        out[i] = ((int)a[i] + (int)b[i]) / 2;
    }
}

/* w and h are the size of input buffer; the output buffer will have the dimensions swapped */
static void transpose(int16_t * in, int16_t * out, int w, int h)
{
    #pragma omp parallel for
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            out[y + x*h] = in[x + y*w];
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

static void horizontal_edge_aware_blur_rggb(
    int16_t * in_r,  int16_t * in_g1,  int16_t * in_g2,  int16_t * in_b,
    int16_t * out_r, int16_t * out_g1, int16_t * out_g2, int16_t * out_b,
    int w, int h, int edge_thr, int strength_lo, int strength_hi, int strength_thr)
{
    const int NMAX = 256;
    int g1[NMAX];
    int g2[NMAX];
    int rg[NMAX];
    int bg[NMAX];
    if (MAX(strength_lo, strength_hi) > NMAX)
    {
        printf("FIXME: blur too strong\n");
        return;
    }

    /* precompute average green, red-green and blue-green */
    int16_t * avg_g  = malloc(w * h * sizeof(avg_g[0]));
    int16_t * dif_rg = malloc(w * h * sizeof(dif_rg[0]));
    int16_t * dif_bg = malloc(w * h * sizeof(dif_bg[0]));
    average(in_g1, in_g2, avg_g, w, h);
    subtract(in_r, avg_g, dif_rg, w, h);
    subtract(in_b, avg_g, dif_bg, w, h);

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            int p0 = avg_g[x + y*w];
            int num = 0;
            
            /* use different filter strength for highlights vs rest of the picture */
            int strength = (p0 < strength_thr) ? strength_lo/2 : strength_hi/2;

            /* range of pixels similar to p0 */
            /* it will contain at least 1 pixel, and at most from 2*strength + 1 pixels */
            int xl = x-1;
            int xr = x+1;

            /* go to the right, until crossing the threshold */
            while (xr < MIN(x + strength, w))
            {
                int p = avg_g[xr + y*w];
                if (abs(p - p0) > edge_thr)
                    break;
                xr++;
            }

            /* same, to the left */
            while (xl >= MAX(x - strength, 0))
            {
                int p = avg_g[xl + y*w];
                if (abs(p - p0) > edge_thr)
                    break;
                xl--;
            }
            
            /* now take the medians from this interval */
            for (int xx = xl+1; xx < xr; xx++)
            {
                g1[num] = in_g1[xx + y*w];
                g2[num] = in_g2[xx + y*w];
                rg[num] = dif_rg[xx + y*w];
                bg[num] = dif_bg[xx + y*w];
                num++;
            }
            
            int mg1 = median_int_wirth(g1, num);
            int mg2 = median_int_wirth(g2, num);
            int mg = (mg1 + mg2) / 2;
            out_g1[x + y*w] = mg1;
            out_g2[x + y*w] = mg2;
            out_r [x + y*w] = median_int_wirth(rg, num) + mg;
            out_b [x + y*w] = median_int_wirth(bg, num) + mg;
        }
    }
    
    free(avg_g);
    free(dif_rg);
    free(dif_bg);
}

/* Find and apply a scalar offset to each column, to reduce pattern noise */
/* original: input and output */
/* denoised: input only */
static void fix_column_noise(int16_t * original, int16_t * denoised, int w, int h, int clip_thr, int nonlinear_highlights)
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
            int hgradient = abs(hgrad[x + y*w]);
            int noise_val = noise[x + y*w];

            mask[x + y*w] = 
                (noise_val == 0)  ||    /* hack: figure out why does this appear to give much better results, and whether there are side effects */
                (hgradient > 500) ||    /* mask out pixels on a strong edge, that is clearly not pattern noise */
                (nonlinear_highlights ? /* row noise is very different in nonlinear (nearly clipped) highlights, compared to the rest of the image */
                       pixel <= clip_thr : /* NL highlights: mask out normally-exposed areas */ 
                       pixel > clip_thr ); /* regular image: mask out nearly-overexposed pixels */
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
            original[i] = mask[i] * 1000;
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
            int pixel = original[x + y*w];
            if (nonlinear_highlights ? pixel > clip_thr : pixel <= clip_thr)
            {
                original[x + y*w] = COERCE((int)original[x + y*w] + col_offsets[x] - mc, -32767, 32760);
            }
        }
    }

end:
    free(noise);
    free(col_offsets);
    free(noise_row);
    free(mask);
    free(hgrad);
}

/* extract a color channel from a Bayer image */
/* w and h are the size of the input buffer; output will be half-res */
/* dx and dy can be 0 or 1 */
static void extract_channel(int16_t * in, int16_t * out, int w, int h, int dx, int dy)
{
    for (int y = dy; y < h; y += 2)
    {
        for (int x = dx; x < w; x += 2)
        {
            out[(x/2) + (y/2)*(w/2)] = in[x + y*w];
        }
    }
}

/* set a color channel into a Bayer image */
/* w and h are the size of the output buffer (full-size image); input will be half-res */
/* dx and dy can be 0 or 1 */
static void set_channel(int16_t * out, int16_t * in, int w, int h, int dx, int dy)
{
    for (int y = dy; y < h; y += 2)
    {
        for (int x = dx; x < w; x += 2)
        {
            out[x + y*w] = in[(x/2) + (y/2)*(w/2)];
        }
    }
}

/* denoised is optional */
static void fix_column_noise_rggb(int16_t * raw, int16_t * denoised, int w, int h, int white)
{
    /* assume Bayer order [GB;RG] */
    int16_t * r        = malloc(w/2 * h/2 * sizeof(r[0]));   /* red channel (bottom left) */
    int16_t * g1       = malloc(w/2 * h/2 * sizeof(r[0]));   /* top-left green */
    int16_t * g2       = malloc(w/2 * h/2 * sizeof(r[0]));   /* bottom-right green */
    int16_t * b        = malloc(w/2 * h/2 * sizeof(r[0]));   /* blue channel (top right) */
    int16_t * rs       = malloc(w/2 * h/2 * sizeof(r[0]));   /* r  after smoothing */
    int16_t * g1s      = malloc(w/2 * h/2 * sizeof(r[0]));   /* g1 after smoothing */
    int16_t * g2s      = malloc(w/2 * h/2 * sizeof(r[0]));   /* g2 after smoothing */
    int16_t * bs       = malloc(w/2 * h/2 * sizeof(r[0]));   /* b  after smoothing */
    
    int16_t* bayer0[4] = {r, g1, g2, b};
    int16_t* bayers[4] = {rs, g1s, g2s, bs};
    
    double t0,t1,t2,t3,t4;
    t0 = omp_get_wtime();
    /* extract half-res color channels from Bayer data */
    # pragma omp parallel for
    for (int k = 0; k < 4; k++)
    {
        extract_channel(raw, bayer0[k],  w, h, (k/2)%2, (k+1)%2);
    }
    t1 = omp_get_wtime();
    

    /* fixme: test */
    int clip_thr = 3900*8;
    
    if (denoised)
    {
        # pragma omp parallel for
        for (int k = 0; k < 4; k++)
        {
            extract_channel(raw, bayers[k],  w, h, (k/2)%2, (k+1)%2);
        }
    }
    else
    {
        /* strong horizontal denoising (1-D median blur on G, R-G and B-G, stop on edge */
        /* (this step takes a lot of time) */
        horizontal_edge_aware_blur_rggb(r, g1, g2, b, rs, g1s, g2s, bs, w/2, h/2, 200, 50, 250, clip_thr);
    }

    t2 = omp_get_wtime();

    /* after blurring horizontally, the difference reveals vertical FPN */

    /* fix for both highlights and normally-exposed images */
    /* (could be probably optimized for speed a bit) */
    
    /* disable highlight processing when showing debug information */
    int hl_en = !g_debug_flags;
    for (int hl = 0; hl <= hl_en; hl++)
    {
        # pragma omp parallel for
        for (int k = 0; k < 4; k++)
        {
            fix_column_noise(bayer0[k],  bayers[k],  w/2, h/2, clip_thr, hl);
        }
    }

    t3 = omp_get_wtime();

    /* commit changes */
    # pragma omp parallel for
    for (int k = 0; k < 4; k++)
    {
        set_channel(raw, bayer0[k], w, h, (k/2)%2, (k+1)%2);
    }

    t4 = omp_get_wtime();

    printf(" (%.2f %.2f %.2f %.2f)", t1-t0, t2-t1, t3-t2, t4-t3);

    /* cleanup */
    free(r);
    free(g1);
    free(g2);
    free(b);
    free(rs);
    free(g1s);
    free(g2s);
    free(bs);
}

void fix_pattern_noise_ex(struct raw_info * raw_info, int16_t * raw, int16_t * denoised, int row_noise_only, int debug_flags)
{
    printf("Fixing %s noise...", row_noise_only ? "row" : "pattern");
    fflush(stdout);

    /* assume Bayer order [GB;RG] */
    if (raw_info->cfa_pattern != 0x01000201)
    {
        printf("Bayer order error\n");
        return;
    }
    
    g_debug_flags = debug_flags;
    
    int w = raw_info->width;
    int h = raw_info->height;

    /* in raw16, data is multiplied by 8 */
    /* we need the white level to ignore overexposed areas when looking for pattern noise */
    int white = raw_info->white_level * 8;
    
    /* fix vertical noise, then transpose and repeat for the horizontal one */
    /* not very efficient, but at least avoids duplicate code */
    /* note: when debugging, we process only one direction */
    if (!row_noise_only && (!g_debug_flags || (g_debug_flags & FIXPN_DBG_COLNOISE)))
    {
        fix_column_noise_rggb(raw, denoised, w, h, white);
    }
    
    if (row_noise_only || !g_debug_flags || !(g_debug_flags & FIXPN_DBG_COLNOISE))
    {
        double t0,t1,t2,t3;
        t0 = omp_get_wtime();

        /* transpose, process just like before, then transpose back */
        int size = w * h * sizeof(raw[0]);
        int16_t * raw_t = malloc(size);
        int16_t * denoised_t = denoised ? malloc(size) : 0;
        transpose(raw, raw_t, w, h);
        if (denoised_t) transpose(denoised, denoised_t, w, h);
        t1 = omp_get_wtime();
        fix_column_noise_rggb(raw_t, denoised_t, h, w, white);
        t2 = omp_get_wtime();
        transpose(raw_t, raw, h, w);
        free(raw_t);
        if (denoised_t) free(denoised_t);
        t3 = omp_get_wtime();
        printf(" (%.2f %.2f %.2f)", t1-t0, t2-t1, t3-t2);
    }
    
    printf("\n");
}

void fix_pattern_noise(struct raw_info * raw_info, int16_t * raw16, int row_noise_only, int debug_flags)
{
    fix_pattern_noise_ex(raw_info, raw16, 0, row_noise_only, debug_flags);
}
