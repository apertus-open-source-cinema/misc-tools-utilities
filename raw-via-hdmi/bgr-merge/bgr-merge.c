#include "stdint.h"
#include <stdio.h>

int main(int argc, char *argv[])
{
    int lines = 1080;
    int coloumns = 1920;

    if (argc == 4)
    {
        printf("file 1: %s\n", argv[1]);
        printf("file 2: %s\n", argv[2]);
        printf("output: %s\n", argv[3]);
    }
    else
    {
        return 0;
    }

    unsigned char buffer1_bgr[coloumns * 3];
    unsigned char buffer1_rgb[coloumns * 3];
    unsigned char buffer2_bgr[coloumns * 3];
    unsigned char buffer2_rgb[coloumns * 3];

    FILE *ptr1;
    FILE *ptr2;

    ptr1 = fopen(argv[1], "rb");
    ptr2 = fopen(argv[2], "rb");

    FILE *write_ptr;

    write_ptr = fopen(argv[3], "wb");

    for (int i = 0;i < lines;i++)
    {
        fread(buffer1_bgr, sizeof(buffer1_bgr), 1, ptr1);
        for (int j = 0;j < coloumns * 3;j++) {
            if (j%3 == 0) { // B input pixel
                buffer1_rgb[j] = buffer1_bgr[j+2];
            }
            if (j%3 == 1) { // G input pixel
                buffer1_rgb[j] = buffer1_bgr[j];
            }
            if (j%3 == 2) { // R input pixel
                buffer1_rgb[j] = buffer1_bgr[j-2];
            }
        }
        
        fread(buffer2_bgr, sizeof(buffer2_bgr), 1, ptr2);
        for (int j = 0;j < coloumns * 3;j++) {
            if (j%3 == 0) { // B input pixel
                buffer2_rgb[j] = buffer2_bgr[j+2];
            }
            if (j%3 == 1) { // G input pixel
                buffer2_rgb[j] = buffer2_bgr[j];
            }
            if (j%3 == 2) { // R input pixel
                buffer2_rgb[j] = buffer2_bgr[j-2];
            }
        }

        fwrite(buffer1_rgb, coloumns * 3, 1, write_ptr);
        fwrite(buffer2_rgb, coloumns * 3, 1, write_ptr);
    }

    fclose(write_ptr);

    return 0;
}
