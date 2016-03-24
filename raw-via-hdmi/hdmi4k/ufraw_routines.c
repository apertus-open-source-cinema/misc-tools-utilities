/* Routines adapted from ufraw */
#include "stdint.h"
#include "stdio.h"
#define CLASS

#define PIX_SORT(a,b) { if ((a)>(b)) PIX_SWAP((a),(b)); }
#define PIX_SWAP(a,b) { int temp=(a);(a)=(b);(b)=temp; }

static inline int median9(int *p)
{
    PIX_SORT(p[1], p[2]) ;
    PIX_SORT(p[4], p[5]) ;
    PIX_SORT(p[7], p[8]) ;
    PIX_SORT(p[0], p[1]) ;
    PIX_SORT(p[3], p[4]) ;
    PIX_SORT(p[6], p[7]) ;
    PIX_SORT(p[1], p[2]) ;
    PIX_SORT(p[4], p[5]) ;
    PIX_SORT(p[7], p[8]) ;
    PIX_SORT(p[0], p[3]) ;
    PIX_SORT(p[5], p[8]) ;
    PIX_SORT(p[4], p[7]) ;
    PIX_SORT(p[3], p[6]) ;
    PIX_SORT(p[1], p[4]) ;
    PIX_SORT(p[2], p[5]) ;
    PIX_SORT(p[4], p[7]) ;
    PIX_SORT(p[4], p[2]) ;
    PIX_SORT(p[6], p[4]) ;
    PIX_SORT(p[4], p[2]) ;
    return(p[4]) ;
}

#undef PIX_SWAP
#undef PIX_SORT

#define IMAGE(row,col,ch) image[3*(col)+(ch) + (row)*width*3]
#define RED(row,col)   IMAGE(row,col,0)
#define GREEN(row,col) IMAGE(row,col,1)
#define BLUE(row,col)  IMAGE(row,col,2)

#define DTOP(x) ((x>65535) ? (unsigned short)65535 : (x<0) ? (unsigned short)0 : (unsigned short) x)

// Just making this function inline speeds up ahd_interpolate_INDI() up to 15%
static inline uint16_t eahd_median(int row, int col, int color,
                                 uint16_t* image, const int width)
{
    //declare the pixel array
    int pArray[9];

    //perform the median filter (this only works for red or blue)
    //  result = median(R-G)+G or median(B-G)+G
    //
    // to perform the filter on green, it needs to be the average
    //  results = (median(G-R)+median(G-B)+R+B)/2
    
    //no checks are done here to speed up the inlining
    pArray[0] = (int) IMAGE(row    , col + 1, color) - GREEN(row    , col + 1);
    pArray[1] = (int) IMAGE(row - 1, col + 1, color) - GREEN(row - 1, col + 1);
    pArray[2] = (int) IMAGE(row - 1, col    , color) - GREEN(row - 1, col    );
    pArray[3] = (int) IMAGE(row - 1, col - 1, color) - GREEN(row - 1, col - 1);
    pArray[4] = (int) IMAGE(row    , col - 1, color) - GREEN(row    , col - 1);
    pArray[5] = (int) IMAGE(row + 1, col - 1, color) - GREEN(row + 1, col - 1);
    pArray[6] = (int) IMAGE(row + 1, col    , color) - GREEN(row + 1, col    );
    pArray[7] = (int) IMAGE(row + 1, col + 1, color) - GREEN(row + 1, col + 1);
    pArray[8] = (int) IMAGE(row    , col    , color) - GREEN(row    , col    );

    int result = median9(pArray) + GREEN(row,col);
    return DTOP(result);
}

// Add the color smoothing from Kimmel as suggested in the AHD paper
// Algorithm updated by Michael Goertz
void CLASS color_smooth(uint16_t* image, const int width, const int height,
                        const int passes)
{
    int row, col;
    int row_start = 2;
    int row_stop  = height - 2;
    int col_start = 2;
    int col_stop  = width - 2;
    //interate through all the colors
    int count;

    for (count = 0; count < passes; count++) {
        fprintf(stderr, "Color smooth (%d/%d)...\n", count+1, passes);
        //perform 3 iterations - seems to be a commonly settled upon number of iterations
        for (row = row_start; row < row_stop; row++) {
            for (col = col_start; col < col_stop; col++) {
                //calculate the median only over the red and blue
                //calculating over green seems to offer very little additional quality
                RED(row,col) = eahd_median(row, col, 0, image, width);
                BLUE(row,col) = eahd_median(row, col, 2, image, width);
            }
        }
    }
}
