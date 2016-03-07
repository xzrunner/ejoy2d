#include "ej_gtxt.h"
#include "sprite.h"
#include "screen.h"
#include "spritepack.h"
#include "ej_utility.h"
#include "ej_shaderlab.h"

#include <gtxt_richtext.h>
#include <gtxt_label.h>
#include <dtex_facade.h>
#include <dtex_package.h>
#include <rvg.h>
#include <shaderlab.h>

#include <string.h>
#include <stdlib.h>
#include <assert.h>

struct symbol {
	const char pkg[64];
	const char export[64];
	struct sprite* spr;
};

struct render_params {
	struct sprite* spr;
	const struct srt* srt;
	const struct sprite_trans* trans;
};

#define SYMBOL_MAX 128

static struct symbol SYM_BUF[SYMBOL_MAX];
static int SYM_SZ = 0;

static struct sprite* 
new_sprite(struct dtex_package* pkg, int id) {
	struct sprite* spr = (struct sprite*)malloc(sizeof(struct sprite));
	int spr_sz = sprite_size(pkg, id);
	sprite_init(spr, pkg, id, spr_sz);

	int idx = 0;
	while (true) {
		int child_id = sprite_component(spr, idx);
		if (child_id < 0) {
			break;
		}
		struct sprite* child = new_sprite(pkg, child_id);
		if (child) {
			child->name = sprite_childname(spr, child_id);
			sprite_mount(spr, idx, child);
		}

		++idx;
	}

	return spr;
}

static void*
ext_sym_create(const char* str) {
	if (strncmp(str, "pkg=", 4) != 0) {
		return NULL;
	}

	int len = 0;
	while (str[4 + len] != ' ') {
		++len;
	}
	char s_pkg[64], s_export[64];	
	strncpy(s_pkg, &str[4], len);
	s_pkg[len] = 0;
	strcpy(s_export, &str[4+len+8]);	// " export="

	for (int i = 0; i < SYM_SZ; ++i) {
		struct symbol* sym = &SYM_BUF[i];
		if (strcmp(sym->pkg, s_pkg) == 0 &&
			strcmp(sym->export, s_export) == 0) {
			return sym->spr;
		}
	}

	if (SYM_SZ >= SYMBOL_MAX) {
		SYM_SZ -= 1;
	}

	struct symbol* sym = &SYM_BUF[SYM_SZ++];
	strcpy(sym->pkg, s_pkg);
	strcpy(sym->export, s_export);

	struct dtex_package* pkg = dtexf_query_pkg(s_pkg);
	int spr_id = dtex_get_spr_id(pkg, s_export);
	sym->spr = new_sprite(pkg, spr_id);

	return sym->spr;
}

static void
ext_sym_release(void* ext_sym) {
	if (!ext_sym) {
		return;
	}

	// 	struct sprite* spr = (struct sprite*)ext_sym;
	// 	free(spr);
}

static void 
ext_sym_get_size(void* ext_sym, int* width, int* height) {
	if (!ext_sym) {
		*width= *height = 0;
		return;
	}

	struct sprite* spr = (struct sprite*)ext_sym;
	int aabb[4];
	sprite_aabb(spr, NULL, false, aabb);

	*width = aabb[2] - aabb[0];
	*height = aabb[3] - aabb[1];
}

static void
ext_sym_render(void* ext_sym, float x, float y, void* ud) {
	if (!ext_sym) {
		return;
	}

	struct sprite* spr = (struct sprite*)ext_sym;
	struct render_params* rp = (struct render_params*)ud;

	struct srt _srt = *rp->srt;
	_srt.offx += x * SCREEN_SCALE * _srt.scalex / 1024;
	_srt.offy += y * SCREEN_SCALE * _srt.scaley / 1024;

	sprite_draw_with_trans(spr, &_srt, rp->trans);
}

static bool
ext_sym_query(void* ext_sym, float x, float y, float w, float h, int qx, int qy, void* ud) {
	if (!ext_sym) {
		return NULL;
	}

//	struct sprite* spr = (struct sprite*)ext_sym;
	struct srt* srt =  (struct srt*)ud;

	qx /= SCREEN_SCALE;
	qy /= SCREEN_SCALE;
	qx -= x * srt->scalex / 1024;
	qy -= y * srt->scaley / 1024;

	float hw = w * 0.5f,
		hh = h * 0.5f;
	return (qx >= -hw && qx <= hw && qy >= -hh && qy <= hh);
}

void 
ej_gtxt_init() {
	gtxt_richtext_ext_sym_cb_init(&ext_sym_create, &ext_sym_release, &ext_sym_get_size, &ext_sym_render, &ext_sym_query);
}

static void
_draw_text_glyph(int id, float* texcoords, float x, float y, float w, float h, struct gtxt_draw_style* ds, int* mat, const struct sprite_trans* t) {
	x += ds->offset_x;
	y += ds->offset_y;
	float hw = w * 0.5f * ds->scale, hh = h * 0.5f * ds->scale;

	float vb[8], tb[8];

	vb[0] = x - hw;	vb[1] = y + hh;
	vb[2] = x - hw;	vb[3] = y - hh;
	vb[4] = x + hw;	vb[5] = y - hh;
	vb[6] = x + hw;	vb[7] = y + hh;

// 	tb[0] = texcoords[0] * 0xffff; tb[1] = texcoords[1] * 0xffff;
// 	tb[2] = texcoords[2] * 0xffff; tb[3] = texcoords[3] * 0xffff;
// 	tb[4] = texcoords[4] * 0xffff; tb[5] = texcoords[5] * 0xffff;
// 	tb[6] = texcoords[6] * 0xffff; tb[7] = texcoords[7] * 0xffff;
	tb[0] = texcoords[0]; tb[1] = texcoords[1];
	tb[2] = texcoords[2]; tb[3] = texcoords[3];
	tb[4] = texcoords[4]; tb[5] = texcoords[5];
	tb[6] = texcoords[6]; tb[7] = texcoords[7];

	for (int i = 0; i < 4; ++i) {
		float xx = vb[i * 2] * SCREEN_SCALE;
		float yy =-vb[i * 2 + 1] * SCREEN_SCALE;
		float vx = (xx * mat[0] + yy * mat[2]) / 1024 + mat[4];
		float vy = (xx * mat[1] + yy * mat[3]) / 1024 + mat[5];
		screen_trans(&vx,&vy);
		vb[i * 2] = vx;
		vb[i * 2 + 1] = vy;
	}

	uint32_t color = 0x00ffffff | (uint32_t)(0xff000000 * ds->alpha);
	if (t->color != 0xffffffff) {
		color = color_mul(color, t->color);
	}

	ej_sl_sprite();
	sl_sprite_set_color(t->color, t->additive);
	sl_sprite_set_map_color(t->rmap, t->gmap, t->bmap);
	sl_sprite_draw(vb, tb, id);
}

static void
_draw_text_decoration(float x, float y, float w, float h, struct gtxt_draw_style* ds, int* mat) {
	struct gtxt_decoration* d = &ds->decoration;
	if (d->type == GRDT_NULL) {
		return;
	}

	ej_sl_shape();

	uint32_t color = d->color;
	uint8_t r = ((color >> 24) & 0xff);
	uint8_t g = ((color >> 16) & 0xff);
	uint8_t b = ((color >> 8) & 0xff);
	uint8_t a = (color & 0xff);
	color = (a << 24) | (b << 16) | (g << 8) | r;
	sl_shape_color(color);

	float hw = w * 0.5f;
	if (d->type == GRDT_OVERLINE || d->type == GRDT_UNDERLINE || d->type == GRDT_STRIKETHROUGH) {
		float left_x = x - hw, left_y = y;
		float right_x = x + hw, right_y = y;
		switch (d->type) 
		{
		case GRDT_OVERLINE:
			left_y = right_y = - (ds->row_y + ds->row_h);
			break;
		case GRDT_UNDERLINE:
			left_y = right_y = - ds->row_y;
			break;
		case GRDT_STRIKETHROUGH:
			left_y = right_y = - (ds->row_y + ds->row_h * 0.5f);
			break;
		}
		trans_vec2_mat(&left_x, &left_y, mat);
		trans_vec2_mat(&right_x, &right_y, mat);
		rvg_line(left_x, left_y, right_x, right_y);
	} else if (d->type == GRDT_BORDER || d->type == GRDT_BG) {
		float min_x = x - hw, min_y = ds->row_y;
		float max_x = x + hw, max_y = ds->row_y + ds->row_h;
		trans_vec2_mat(&min_x, &min_y, mat);
		trans_vec2_mat(&max_x, &max_y, mat);
		if (d->type == GRDT_BG) {
			rvg_rect(min_x, min_y, max_x, max_y, true);
		} else if (ds->pos_type != GRPT_NULL) {
			rvg_line(min_x, min_y, max_x, min_y);
			rvg_line(min_x, max_y, max_x, max_y);
			if (ds->pos_type == GRPT_BEGIN) {
				rvg_line(min_x, min_y, min_x, max_y);
			}
			if (ds->pos_type == GRPT_END) {
				rvg_line(max_x, min_y, max_x, max_y);
			}
		}
	}
}

static void
init_gtxt_label_style(struct gtxt_label_style* style, struct pack_label* pl) {
	style->width = pl->width;
	style->height = pl->height;

	style->align_h = pl->align_hori;
	style->align_v = pl->align_vert;

	style->space_h = pl->space_hori;
	style->space_v = pl->space_vert;

	style->gs.font = pl->font;
	style->gs.font_size = pl->font_size;
	style->gs.font_color.integer = pl->font_color;

	style->gs.edge = pl->edge;
	style->gs.edge_size = pl->edge_size;
	style->gs.edge_color.integer = pl->edge_color;
}

static void
draw_text(int id, float* texcoords, float x, float y, float w, float h, struct gtxt_draw_style* ds, void* ud) {
	struct render_params* rp = (struct render_params*)ud;

	struct matrix tmp;
	if (rp->trans->mat == NULL) {
		matrix_identity(&tmp);
	} else {
		tmp = *rp->trans->mat;
	}
	matrix_srt(&tmp, rp->srt);
	int* mat = tmp.m;	

	if (ds->decoration.type == GRDT_BG) {
		_draw_text_decoration(x, y, w, h, ds, mat);
		_draw_text_glyph(id, texcoords, x, y, w, h, ds, mat, rp->trans);
	} else {
		_draw_text_glyph(id, texcoords, x, y, w, h, ds, mat, rp->trans);
		_draw_text_decoration(x, y, w, h, ds, mat);
	}
}

void 
ej_gtxt_draw(struct sprite* spr, const struct srt* srt, const struct sprite_trans* trans) {
	//const char* str = s->data.text ? s->data.text : s->s.label->text;
	const char* str = spr->data.text;
	if (!str || str[0] == 0) {
		return;
	}

	struct gtxt_label_style style;
	init_gtxt_label_style(&style, spr->s.label);

	struct render_params rp;
	rp.srt = srt;
	rp.trans = trans;

	gtxt_label_draw_richtext(str, &style, spr->time, &draw_text, &rp);

	sl_shader_flush();
}

void 
ej_gtxt_get_size(struct sprite* spr, float* width, float* height) {
	assert(spr->type == TYPE_LABEL);
	struct gtxt_label_style style;
	init_gtxt_label_style(&style, spr->s.label);
	gtxt_get_label_size(spr->data.text, &style, width, height);	
}

struct sprite* 
ej_gtxt_point_query(struct sprite* spr, int x, int y, const struct srt* srt) {
	if (!spr->data.text) {
		return NULL;
	}

	struct gtxt_label_style style;
	init_gtxt_label_style(&style, spr->s.label);
	void* find = gtxt_label_point_query(spr->data.text, &style, x, y, (void*)srt);
	// todo return sprite in richtext directly
	return (struct sprite*)find;
}