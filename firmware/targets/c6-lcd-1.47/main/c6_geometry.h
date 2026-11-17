#pragma once
#include <stdbool.h>
#include <stdint.h>
bool c6_touch_rotate(uint16_t raw_x,uint16_t raw_y,uint16_t rotation,uint16_t *x,uint16_t *y);
uint16_t c6_gravity_rotation(int x_mg,int y_mg,int z_mg,uint16_t previous);
