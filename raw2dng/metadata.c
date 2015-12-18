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

/* based on metadatareader.c */
void extract_metadata(uint16_t registers[128])
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
}
