#ifndef ej_utility_h
#define ej_utility_h

#include "spritepack.h"

extern inline void
trans_vec2_mat(float* x, float* y, int* mat);

extern inline uint32_t
color_mul(uint32_t c1, uint32_t c2);

#endif // ej_utility_h