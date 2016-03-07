#include "ej_rvg.h"
#include "ej_utility.h"
#include "matrix.h"
#include "adapter/ej_shaderlab.h"

#include <rvg.h>
#include <shaderlab.h>

void 
ej_rvg_draw(struct pack_shape* shape, const struct srt* srt, const struct sprite_trans* trans) {
	struct matrix tmp;
	if (trans->mat == NULL) {
		matrix_identity(&tmp);
	} else {
		tmp = *trans->mat;
	}
	matrix_srt(&tmp, srt);
	int* mat = tmp.m;

	ej_sl_shape();

	uint32_t color = shape->color;
	uint8_t r = ((color >> 24) & 0xff);
	uint8_t g = ((color >> 16) & 0xff);
	uint8_t b = ((color >> 8) & 0xff);
	uint8_t a = (color & 0xff);
	color = (a << 24) | (b << 16) | (g << 8) | r;
	color = color_mul(color, trans->color);
	sl_shape_color(color);

	float buf[shape->num * 2];
	int ptr = 0;
	for (int i = 0; i < shape->num; ++i) {
		float x = shape->vertices[ptr] / 16.0f,
			y = shape->vertices[ptr+1] / 16.0f;
		trans_vec2_mat(&x, &y, mat);
		buf[ptr] = x;
		buf[ptr+1] = y;
		ptr += 2;
	}
	rvg_triangles(buf, shape->num);
}