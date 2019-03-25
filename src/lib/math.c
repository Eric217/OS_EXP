#include "math.h"

int pow(int x, int y) {
    int n = 1;
    while (y) {
        if (y & 1) 
        	n *= x;
        x *= x;
        y = y >> 1;
    }
    
    return n;
}