/* based on metadatareader */
/* extract metadata from the 128x16bit register block, and copy it to DNG (EXIF) */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include "chdk-dng.h"

typedef int bool;
#define true 1
#define false 0

// Contributed by Herbert Poetzl, (loosely) based on CMV12000 documentation
static inline  
double  exposure(uint32_t time, int reg82, int reg85, double bits, double lvds)  
{
        double fot_overlap = (34 * (reg82 & 0xFF)) + 1;  
 
        return ((time - 1)*(reg85 + 1) + fot_overlap) *  
                (bits/lvds) * 1e3;  
} 

// extract range of bits with offset and length from LSB
static uint16_t get_bits (uint16_t in, int offset, int length) {
    uint16_t in1 = in >> offset;    
    uint16_t in2 = in1 << (16-length);  
    uint16_t in3 = in2 >> (16-length);  
    return in3;
}

static double get_exposure(uint16_t registers[128])
{
    double bits;
    switch (get_bits(registers[118], 0, 2)) {
        case 0:
            bits = 12;
            break;
        case 1:
            bits = 10;
            break;
        case 2:
            bits = 8;
            break;
        default:
            return 0;
    }
    double lvds = 250e6;
    return exposure(registers[72]*65536+registers[71], registers[82], registers[85], bits, lvds);
}

static int get_div(uint16_t registers[128])
{
    int pga_div = get_bits(registers[115], 3, 1);
    return pga_div;
}

static int get_gain(uint16_t registers[128])
{
    int pga_gain = get_bits(registers[115], 0, 3);
    switch (pga_gain)
    {
        case 0:
            return 1;
        case 1:
            return 2;
        case 3:
            return 3;
        case 7:
            return 4;
    }
    
    return 0;
}

static void dump_registers(uint16_t registers[128])
{
    const char * reg_names[128] = {
        [1]           = "Number_lines_tot",
        [2  ... 33]   = "Y_start",
        [34 ... 65]   = "Y_size",
        [66]          = "Sub_offset",
        [67]          = "Sub_step",
        [68]          = "Color, Sub_en, Bin_en, Color_exp",
        [69]          = "Image_flipping",
        [70]          = "Exp_dual, exp_ext",
        [71 ... 72]   = "Exp_time",
        [73 ... 74]   = "Exp_time2",
        [75 ... 76]   = "Exp_kp1",
        [77 ... 78]   = "Exp_kp2",
        [79]          = "Number_slopes",
        [80]          = "Number_frames",
        [81]          = "Output_mode, Disable_top",
        [82 ... 86]   = "Setting 1..5",
        [87 ... 88]   = "Dark offset top/bot",
        [89]          = "Training_pattern, Black_col_en",
        [90 ... 94]   = "Channel_en top/bot",
        [95 ... 96]   = "ADC_clk_en top/bot",
        [98]          = "Setting '98'",         /* not named in datasheet,
                                                   but looks similar to Setting 1..7. */
        [102]         = "Black sun protection", /* not in datasheet; see AN01 and AN02 */
        [106]         = "Vtfl",
        [107]         = "Clock speed tuning",
        [109]         = "Vramp",
        [113 ... 114] = "Setting 6..7",
        [115]         = "PGA_gain, PGA_div",
        [116]         = "ADC_range, ADC_range_mult",
        [117]         = "DIG_gain",
        [118]         = "Bit_mode",
        [122]         = "Test_Pattern",
        [127]         = "Temp_sensor",
    };
    
    /* registers with fixed value / do not change */
    const int fv_dnc_regs[128] = {
        [0 ... 127]   = -1,
        [0]           = 0,
        [69]          = 2,
        [97]          = 0,
        [99]          = 34952,
        [100 ... 101] = 0,
        [103]         = 4032,
        [104]         = 64,
        [105]         = 8256,
        [108]         = 12381,
        [110]         = 12368,
        [111]         = 34952,
        [112]         = 277,
        [119]         = 0,
        [120]         = 9,
        [121]         = 1,
        [123]         = 0,
        [124]         = 15,
        [125]         = 2,
        [126]         = 770,
    };
    
    for (int i = 0; i < 128; i++)
    {
        if (fv_dnc_regs[i] >= 0)
        {
            /* for those registers, just check the expected value,
             * and print a warning if it doesn't match
             */
            if (registers[i] != fv_dnc_regs[i])
            {
                printf("Register %3d: %5d = 0x%-4X (??? expected %d)\n", i, registers[i], registers[i], fv_dnc_regs[i]);
            }
        }
        else
        {
            if (registers[i])
            {
                /* regular registers: print them, but only if their value is nonzero,
                 * to reduce clutter on the output
                 */
                printf("Register %3d: %5d = 0x%-4X", i, registers[i], registers[i]);
                if (reg_names[i]) printf(" (%s)", reg_names[i]);
                printf("\n");
            }
        }
    }
}

/* based on metadatareader.c */
void extract_metadata(uint16_t registers[128], int dump_regs)
{
    double exposure_ms = get_exposure(registers);
    printf("Exposure    : %g ms\n", exposure_ms);
    dng_set_shutter((int)round(exposure_ms * 1000), 1000000);

    int gain = get_gain(registers);
    int div = get_div(registers);

    if (gain)
    {
        printf("Gain        : x%d%s\n", gain, div ? "/3" : "");
        
        /* this one is a really rough guess */
        dng_set_iso(400 * gain / (div ? 3 : 1));
    }

    if (dump_regs)
    {
        dump_registers(registers);
    }
}
