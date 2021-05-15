#include <stdio.h>

int main()
{
    int i, j;
    unsigned char rgb[3] = {128, 0, 128};
    while (1) {
//       for (i = 0; i < 100; i++) {
//            for (j = 0; j < 100; j++) {
                fwrite(rgb, sizeof(rgb), 1, stdout);
//            }
//        }
        for (i = 0; i < 1; i++) {
            rgb[i]++;
        }    
    }

    return 0;
}
