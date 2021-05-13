#include <stdio.h>

int main()
{
    int i, j;
    unsigned char rgb[3] = {10, 60, 128};
    while (1) {
        for (i = 0; i < 480; i++) {
            for (j = 0; j < 640; j++) {
                fwrite(rgb, sizeof(rgb), 1, stdout);
            }
        }

        for (i = 0; i < 3; i++) {
            rgb[i]++;
        }
    }

    return 0;
}
