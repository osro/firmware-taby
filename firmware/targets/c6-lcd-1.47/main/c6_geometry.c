#include "c6_geometry.h"
#include <stdlib.h>
bool c6_touch_rotate(uint16_t rx,uint16_t ry,uint16_t r,uint16_t *x,uint16_t *y) {
    if (!x || !y || rx>=172 || ry>=320) return false;
    switch (r) {
        case 0:*x=171-rx;*y=ry;return true;
        case 90:*x=ry;*y=rx;return true;
        case 180:*x=rx;*y=319-ry;return true;
        case 270:*x=319-ry;*y=171-rx;return true;
        default:return false;
    }
}
uint16_t c6_gravity_rotation(int x,int y,int z,uint16_t previous) {
    int ax=abs(x),ay=abs(y),az=abs(z);
    if (az>850 || (ax<650 && ay<650) || abs(ax-ay)<200) return previous;
    if (ay>ax) return y<0?0:180;
    return x>0?90:270;
}
