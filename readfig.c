/*
* Simple program feeding raw data into device
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main()
{
    int i, j;
    unsigned char rgb[3];// = {10, 60, 128};
    // b g r


    //unsigned char rgb[3] = {0, 0, 0};
    while (1) {
        FILE *file = fopen("sample_image/1.raw", "r");
        for (i = 0; i < 640; i++) {
            for (j = 0; j < 480; j++) {
                memset(rgb, 0, sizeof(rgb));
                fscanf(file,"%hhu %hhu %hhu", &rgb[0], &rgb[1], &rgb[2]);
                fwrite(rgb, sizeof(rgb), 1, stdout);
            }
        }
        //while (fscanf(file,"%hhu %hhu %hhu", rgb, rgb+1, rgb+2) > 0) {
            //printf("%hhu%hhu%hhu\n", rgb[0],rgb[1],rgb[2]);
            //fwrite(rgb, sizeof(rgb), 1, stdout);
        //}
        fclose(file);
    }

    return 0;
}
