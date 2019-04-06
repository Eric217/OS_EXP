#include "math.h"

inline double ceil(double x) {
	double v = (long)x;
	if (x == v)	{
		return v;
	}
	return v + 1;
}

inline int pow(int x, int y) {
    int n = 1;
    while (y) {
        if (y & 1) 
        	n *= x;
        x *= x;
        y = y >> 1;
    }
    
    return n;
}