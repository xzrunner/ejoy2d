#include "ej_utility.h"

inline void
trans_vec2_mat(float* x, float* y, int* mat) {
	float src_x = *x * SCREEN_SCALE,
		src_y = *y * SCREEN_SCALE;
	*x = (src_x * mat[0] + src_y * mat[2]) / 1024 + mat[4];
	*y = (src_x * mat[1] + src_y * mat[3]) / 1024 + mat[5];
	*x /= SCREEN_SCALE;
	*y /= SCREEN_SCALE;
}

inline uint32_t
color_mul(uint32_t c1, uint32_t c2) {
	int r1 = (c1 >> 24) & 0xff;
	int g1 = (c1 >> 16) & 0xff;
	int b1 = (c1 >> 8) & 0xff;
	int a1 = (c1) & 0xff;
	int r2 = (c2 >> 24) & 0xff;
	int g2 = (c2 >> 16) & 0xff;
	int b2 = (c2 >> 8) & 0xff;
	int a2 = c2 & 0xff;

	return (r1 * r2 /255) << 24 |
		(g1 * g2 /255) << 16 |
		(b1 * b2 /255) << 8 |
		(a1 * a2 /255) ;
}