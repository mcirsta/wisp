#include <stdio.h>

int main(void)
{
    int margin = -10;
    unsigned int test1 = -(unsigned int)margin;
    unsigned int test2 = (unsigned int)(-margin);
    unsigned int max_neg = 0;

    if (-(unsigned int)margin > max_neg) {
        printf("Condition 1 TRUE: %u > %u\n", test1, max_neg);
    }

    if ((unsigned int)(-margin) > max_neg) {
        printf("Condition 2 TRUE: %u > %u\n", test2, max_neg);
    }
    return 0;
}
