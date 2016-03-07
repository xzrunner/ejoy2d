#include "sprite.h"
#include "spritepack.h"
#include "screen.h"
#include "matrix.h"
#include "scissor.h"
#include "array.h"

#include "adapter/ej_ps.h"
#include "adapter/ej_rvg.h"
#include "adapter/ej_utility.h"
#include "adapter/ej_gtxt.h"
#include "adapter/ej_shaderlab.h"

#include <dtex.h>
#include <gtxt.h>
#include <shaderlab.h>

#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <limits.h>
#include <float.h>

void
sprite_drawquad(struct sprite* spr, struct pack_picture *picture, const struct srt *srt,  const struct sprite_trans *arg) {
	ej_sl_sprite();

	struct matrix tmp;
	if (arg->mat == NULL) {
		matrix_identity(&tmp);
	} else {
		tmp = *arg->mat;
	}
	matrix_srt(&tmp, srt);
	int *m = tmp.m;
	for (int i = 0; i < picture->n; ++i) {
		struct pack_quad *q = &picture->rect[i];

		struct dtex_texture* tex = dtex_texture_fetch(q->texid);
		int id = tex->id;
		if (id == 0) {
			continue;
		}

		float vb[8], tb[8];

		float xmin = FLT_MAX, ymin = FLT_MAX, xmax = -FLT_MAX, ymax = -FLT_MAX;
		for (int j = 0; j < 4; ++j) {
			int xx = q->screen_coord[j*2+0];
			int yy = q->screen_coord[j*2+1];
			float vx = (xx * m[0] + yy * m[2]) / 1024 + m[4];
			float vy = (xx * m[1] + yy * m[3]) / 1024 + m[5];

			float tx = q->texture_coord[j*2+0];
			float ty = q->texture_coord[j*2+1];
			tx *= tex->inv_width;
			ty *= tex->inv_height;
			if (tex->type == DTEX_TT_RAW) {
				tx *= tex->t.RAW.lod_scale;
				ty *= tex->t.RAW.lod_scale;
			}
// 			tx *= 0xffff;
// 			ty *= 0xffff;

			screen_trans(&vx,&vy);

			if (vx < xmin) xmin = vx;
			if (vx > xmax) xmax = vx;
			if (vy < ymin) ymin = vy;
			if (vy > ymax) ymax = vy;

			vb[j * 2] = vx;
			vb[j * 2 + 1] = vy;
			tb[j * 2] = tx;
			tb[j * 2 + 1] = ty;
		}

		if (xmax < 0 || xmin > 2 || ymax < -2 || ymin > 0) {
			continue;
		}

		int c2_tex_map;
		float* c2_vb_map = dtexf_c2_lookup_texcoords(spr->pkg->id, spr->id, &c2_tex_map);
		if (c2_vb_map) {
			for (int i = 0; i < 4; ++i) {
				float tx = c2_vb_map[i*2];
				float ty = c2_vb_map[i*2+1];
				if (tex->type == DTEX_TT_RAW) {
					tx *= tex->t.RAW.lod_scale;
					ty *= tex->t.RAW.lod_scale;
				}
// 				tx *= 0xffff;
// 				ty *= 0xffff;
				tb[i * 2] = tx;
				tb[i * 2 + 1] = ty;
			}
			id = c2_tex_map;
		} else {
			dtex_c2_on_draw_query_fail(spr);
		}

		sl_sprite_set_color(arg->color, arg->additive);
		sl_sprite_set_map_color(arg->rmap, arg->gmap, arg->bmap);
		sl_sprite_draw(vb, tb, id);
	}
}

int
sprite_size(struct dtex_package* pkg, int id) {
	struct sprite_pack* ej_pkg = pkg->ej_pkg;
	if (id < 0 || id >=	ej_pkg->n)
		return 0;
	int type = ej_pkg->type[id];
	if (type == TYPE_ANIMATION) {
		struct pack_animation * ani = (struct pack_animation *)ej_pkg->data[id];
		return sizeof(struct sprite) + (ani->component_number - 1) * sizeof(struct sprite *);
	} else if (type == TYPE_PARTICLE3D) {
		struct p3d_emitter_cfg* p3d_cfg = (struct p3d_emitter_cfg*)ej_pkg->data[id];
		return sizeof(struct sprite) + (p3d_cfg->symbol_count - 1) * sizeof(struct sprite*);
	} else if (type == TYPE_PARTICLE2D) {
		struct p2d_emitter_cfg* p2d_cfg = (struct p2d_emitter_cfg*)ej_pkg->data[id];
		return sizeof(struct sprite) + (p2d_cfg->symbol_count - 1) * sizeof(struct sprite*);
	} else if (type == TYPE_P3D_SPR) {
		return sizeof(struct sprite) + sizeof(struct sprite*);
	} else {
		return sizeof(struct sprite);
	}
	return 0;
}

int
sprite_action(struct sprite *s, const char * action) {
	if (s->type != TYPE_ANIMATION) {
		return -1;
	}
	struct pack_animation *ani = s->s.ani;
	if (action == NULL) {
		if (ani->action == NULL) {
			return -1;
		}
		s->start_frame = ani->action[0].start_frame;
		s->total_frame = ani->action[0].number;
		s->frame = 0;
		return s->total_frame;
	} else {
		int i;
		for (i=0;i<ani->action_number;i++) {
			const char *name = ani->action[i].name;
			if (name) {
				if (strcmp(name, action)==0) {
					s->start_frame = ani->action[i].start_frame;
					s->total_frame = ani->action[i].number;
					s->frame = 0;
					return s->total_frame;
				}
			}
		}
		return -1;
	}
}

void
sprite_init(struct sprite * s, struct dtex_package* pkg, int id, int sz) {
	struct sprite_pack* ej_pkg = pkg->ej_pkg;

	if (id < 0 || id >=	ej_pkg->n)
		return;
	s->pkg = pkg;
	s->pkg_version = pkg->version;
	s->parent = NULL;
	s->t.mat = NULL;
	s->t.color = 0xffffffff;
	s->t.additive = 0;
	s->t.rmap = 0xff0000ff;
	s->t.gmap = 0x00ff00ff;
	s->t.bmap = 0x0000ffff;
	s->t.program = /*PROGRAM_DEFAULT*/0;
	s->flags = 0;
	s->time = 0;
	s->name = NULL;
	s->id = id;
	s->type = ej_pkg->type[id];
	s->start_frame = 0;
	s->total_frame = 0;
	s->frame = 0;
	if (s->type == TYPE_ANIMATION) {
		struct pack_animation * ani = (struct pack_animation *)ej_pkg->data[id];
		s->s.ani = ani;
		sprite_action(s, NULL);
		int i;
		int n = ani->component_number;
		assert(sz >= sizeof(struct sprite) + (n - 1) * sizeof(struct sprite *));
		for (i=0; i<n ;i++) {
			s->data.children[i] = NULL;
		}
	} else if (s->type == TYPE_PARTICLE3D) {
		struct p3d_emitter_cfg* cfg = (struct p3d_emitter_cfg*)ej_pkg->data[id];
		s->s.p3d = cfg;
		int n = cfg->symbol_count;
		assert(sz >= sizeof(struct sprite) + (n - 1) * sizeof(struct sprite *));
		for (int i = 0; i < n ; ++i) {
			s->data.children[i] = NULL;
		}
	} else if (s->type == TYPE_PARTICLE2D) {
		struct p2d_emitter_cfg* cfg = (struct p2d_emitter_cfg*)ej_pkg->data[id];
		s->s.p2d = cfg;
		int n = cfg->symbol_count;
		assert(sz >= sizeof(struct sprite) + (n - 1) * sizeof(struct sprite *));
		for (int i = 0; i < n ; ++i) {
			s->data.children[i] = NULL;
		}
		struct p2d_emitter* et = p2d_emitter_create(cfg);
		et->active = true;
		s->data_ext.p2d = et;
	} else if (s->type == TYPE_P3D_SPR) {
		struct pack_p3d_spr* spr = (struct pack_p3d_spr*)ej_pkg->data[id];
		s->s.p3d_spr = spr;
		s->data_ext.p3d = NULL;
		s->data.children[0] = NULL;
	} else if (s->type == TYPE_SHAPE) {
		struct pack_shape* shape = (struct pack_shape*)ej_pkg->data[id];
		s->s.shape = shape;
	} else {
		s->s.pic = (struct pack_picture *)ej_pkg->data[id];
		memset(&s->data, 0, sizeof(s->data));
		assert(sz >= sizeof(struct sprite) - sizeof(struct sprite *));
		if (s->type == TYPE_PANNEL) {
			struct pack_pannel * pp = (struct pack_pannel *)ej_pkg->data[id];
			s->data.scissor = pp->scissor;
		}
	}
}

static inline int
get_frame(struct sprite *s) {
	if (s->type != TYPE_ANIMATION) {
		return s->start_frame;
	}
	if (s->total_frame <= 0) {
		return -1;
	}
	int f = s->frame % s->total_frame;
	if (f < 0) {
		f += s->total_frame;
	}
	return f + s->start_frame;
}

bool 
sprite_pkg_valid(struct sprite* spr, int except) {
	if (!spr->pkg) {
		assert(spr->pkg_version == 0);
		return true;
	}
	if (spr->pkg_version != spr->pkg->version) {
		return false;
	}
	if (spr->type != TYPE_ANIMATION) {
		return true;
	}

	int frame = get_frame(spr);
	if (frame < 0) {
		return true;
	}
	struct pack_animation* ani = spr->s.ani;
	struct pack_frame* pf = &ani->frame[frame];
	for (int i = 0; i < pf->n; ++i) {
		struct pack_part* pp = &pf->part[i];
		int index = pp->component_id;
		struct sprite* child = spr->data.children[index];
		if (child == NULL || (child->flags & SPRFLAG_INVISIBLE)) {
			continue;
		}
		if (except != -1 && except == i) {
			continue;
		}
		if (!sprite_pkg_valid(child, -1)) {
			return false;
		}
	}

	return true;
}

void
sprite_mount(struct sprite *parent, int index, struct sprite *child) {
	assert(parent->type == TYPE_ANIMATION 
		|| parent->type == TYPE_PARTICLE3D 
		|| parent->type == TYPE_PARTICLE2D
		|| parent->type == TYPE_P3D_SPR);

	int num = 0;
	if (parent->type == TYPE_ANIMATION) {
		struct pack_animation *ani = parent->s.ani;
		num = ani->component_number;
	} else if (parent->type == TYPE_PARTICLE3D) {
		num = parent->s.p3d->symbol_count;
	} else if (parent->type == TYPE_PARTICLE2D) {
		num = parent->s.p2d->symbol_count;
	} else if (parent->type == TYPE_P3D_SPR) {
		assert(parent->data.children[0] == NULL);
		num = 1;
	}
	
	assert(index >= 0 && index < num);
	struct sprite * oldc = parent->data.children[index];
	if (oldc) {
		oldc->parent = NULL;
		oldc->name = NULL;
	}
	parent->data.children[index] = child;
	if (child) {
		assert(child->parent == NULL);
		if (parent->type == TYPE_ANIMATION &&
			(child->flags & SPRFLAG_MULTIMOUNT) == 0) {
			struct pack_animation *ani = parent->s.ani;
			child->name = ani->component[index].name;
			child->parent = parent;
		}
		if (oldc && oldc->type == TYPE_ANCHOR) {
			if(oldc->flags & SPRFLAG_MESSAGE) {
				child->flags |= SPRFLAG_MESSAGE;
			} else {
				child->flags &= ~SPRFLAG_MESSAGE;
			}
		}
	}

	ej_ps_mount(parent, oldc, child);
}

int
sprite_child(struct sprite *s, const char * childname) {
	assert(childname);
	if (s->type != TYPE_ANIMATION)
		return -1;
	struct pack_animation *ani = s->s.ani;
	int i;
	for (i=0;i<ani->component_number;i++) {
		const char *name = ani->component[i].name;
		if (name) {
			if (strcmp(name, childname)==0) {
				return i;
			}
		}
	}
	return -1;
}

int
sprite_child_ptr(struct sprite *s, struct sprite *child) {
	if (s->type != TYPE_ANIMATION)
		return -1;
	struct pack_animation *ani = s->s.ani;
	int i;
	for (i=0;i<ani->component_number;i++) {
		struct sprite * c = s->data.children[i];
		if (c == child)
			return i;
	}
	return -1;
}

int
sprite_component(struct sprite *s, int index) {
	if (s->type == TYPE_ANIMATION) {
		struct pack_animation *ani = s->s.ani;
		if (index < 0 || index >= ani->component_number) {
			return -1;
		}
		return ani->component[index].id;
	} else if (s->type == TYPE_PARTICLE3D) {
		struct p3d_emitter_cfg* p3d_cfg = s->s.p3d;
		if (index < 0 || index >= p3d_cfg->symbol_count) {
			return -1;
		}
		uint32_t id = (uint32_t)p3d_cfg->symbols[index].ud;
		return (id >> 16) & 0xffff;
	} else if (s->type == TYPE_PARTICLE2D) {
		struct p2d_emitter_cfg* p2d_cfg = (struct p2d_emitter_cfg*)s->s.p2d;
		if (index < 0 || index >= p2d_cfg->symbol_count) {
			return -1;
		}
		uint32_t id = (uint32_t)p2d_cfg->symbols[index].ud;
		return (id >> 16) & 0xffff;
	} else if (s->type == TYPE_P3D_SPR) {
		if (index != 0) {
			return -1;
		}
		
		struct sprite_pack* ej_pkg = s->pkg->ej_pkg;
		struct pack_p3d_spr* spr_cfg = (struct pack_p3d_spr*)ej_pkg->data[s->id];
		return spr_cfg->id;
	}
	
	else {
		return -1;
	}
}

const char *
sprite_childname(struct sprite *s, int index) {
	if (s->type != TYPE_ANIMATION)
		return NULL;
	struct pack_animation *ani = s->s.ani;
	if (index < 0 || index >= ani->component_number)
		return NULL;
	return ani->component[index].name;
}

// draw sprite

static inline unsigned int
clamp(unsigned int c) {
	return ((c) > 255 ? 255 : (c));
}

static inline uint32_t
color_add(uint32_t c1, uint32_t c2) {
	int r1 = (c1 >> 16) & 0xff;
	int g1 = (c1 >> 8) & 0xff;
	int b1 = (c1) & 0xff;
	int r2 = (c2 >> 16) & 0xff;
	int g2 = (c2 >> 8) & 0xff;
	int b2 = (c2) & 0xff;
	return clamp(r1+r2) << 16 |
		clamp(g1+g2) << 8 |
		clamp(b1+b2);
}

struct sprite_trans *
sprite_trans_mul(struct sprite_trans *a, struct sprite_trans *b, struct sprite_trans *t, struct matrix *tmp_matrix) {
	if (b == NULL) {
		return a;
	}

	*t = *a;
	if (t->mat == NULL) {
		t->mat = b->mat;
	} else if (b->mat) {
		matrix_mul(tmp_matrix, t->mat, b->mat);
		t->mat = tmp_matrix;
	}
	if (t->color == 0xffffffff) {
		t->color = b->color;
	} else if (b->color != 0xffffffff) {
		t->color = color_mul(t->color, b->color);
	}
	if (t->additive == 0) {
		t->additive = b->additive;
	} else if (b->additive != 0) {
		t->additive = color_add(t->additive, b->additive);
	}

	if (t->rmap == 0xff0000ff || t->rmap == 0xff000000) {
		t->rmap = b->rmap;
	}
	if (t->gmap == 0x00ff00ff || t->gmap == 0x00ff0000) {
		t->gmap = b->gmap;
	}
	if (t->bmap == 0x0000ffff || t->bmap == 0x0000ff00) {
		t->bmap = b->bmap;
	}

	if (t->program == /*PROGRAM_DEFAULT*/0) {
		t->program = b->program;
	}

	return t;
}

static struct matrix *
mat_mul(struct matrix *a, struct matrix *b, struct matrix *tmp) {
	if (b == NULL)
		return a;
	if (a == NULL)
		return b;
	matrix_mul(tmp, a , b);
	return tmp;
}

static void
set_scissor(const struct pack_pannel *p, const struct srt *srt, const struct sprite_trans *arg) {
	struct matrix tmp;
	if (arg->mat == NULL) {
		matrix_identity(&tmp);
	} else {
		tmp = *arg->mat;
	}
	matrix_srt(&tmp, srt);
	int *m = tmp.m;
	int x[4] = { 0, p->width * SCREEN_SCALE, p->width * SCREEN_SCALE, 0 };
	int y[4] = { 0, 0, p->height * SCREEN_SCALE, p->height * SCREEN_SCALE };
	int minx = (x[0] * m[0] + y[0] * m[2]) / 1024 + m[4];
	int miny = (x[0] * m[1] + y[0] * m[3]) / 1024 + m[5];
	int maxx = minx;
	int maxy = miny;
	int i;
	for (i=1;i<4;i++) {
		int vx = (x[i] * m[0] + y[i] * m[2]) / 1024 + m[4];
		int vy = (x[i] * m[1] + y[i] * m[3]) / 1024 + m[5];
		if (vx<minx) {
			minx = vx;
		} else if (vx > maxx) {
			maxx = vx;
		}
		if (vy<miny) {
			miny = vy;
		} else if (vy > maxy) {
			maxy = vy;
		}
	}
	minx /= SCREEN_SCALE;
	miny /= SCREEN_SCALE;
	maxx /= SCREEN_SCALE;
	maxy /= SCREEN_SCALE;
	scissor_push(minx,miny,maxx-minx,maxy-miny);
}

static void
anchor_update(struct sprite *s, const struct srt *srt, const struct sprite_trans *arg) {
	struct matrix *r = s->s.mat;
	if (arg->mat == NULL) {
		matrix_identity(r);
	} else {
		*r = *arg->mat;
	}
	matrix_srt(r, srt);
}

static void
label_pos(int m[6], struct pack_label * l, int pos[2]) {
	float c_x = l->width * SCREEN_SCALE / 2.0;
	float c_y = l->height * SCREEN_SCALE / 2.0;
	pos[0] = (int)((c_x * m[0] + c_y * m[2]) / 1024 + m[4])/SCREEN_SCALE;
	pos[1] = (int)((c_x * m[1] + c_y * m[3]) / 1024 + m[5])/SCREEN_SCALE;
}

static void
picture_pos(int m[6], struct pack_picture *picture, int pos[2]) {
	int max_x = INT_MIN;
	int max_y = -INT_MAX;
	int min_x = INT_MAX;
	int min_y = INT_MAX;
	int i,j;
	for (i=0;i<picture->n;i++) {
		struct pack_quad *q = &picture->rect[i];
		for (j=0;j<4;j++) {
			int xx = q->screen_coord[j*2+0];
			int yy = q->screen_coord[j*2+1];

			if (xx > max_x) max_x = xx;
			if (yy > max_y) max_y = yy;
			if (xx < min_x) min_x = xx;
			if (yy < min_y) min_y = yy;
		}
	}

	float c_x = (max_x + min_x) / 2.0;
	float c_y = (max_y + min_y) / 2.0;
	pos[0] = (int)((c_x * m[0] + c_y * m[2]) / 1024 + m[4])/SCREEN_SCALE;
	pos[1] = (int)((c_x * m[1] + c_y * m[3]) / 1024 + m[5])/SCREEN_SCALE;
}

uint32_t
color4f(struct ps_color4f *c4f) {
	uint8_t rr = (int)(c4f->r*255);
	uint8_t gg = (int)(c4f->g*255);
	uint8_t bb = (int)(c4f->b*255);
	uint8_t aa = (int)(c4f->a*255);
	return (uint32_t)aa << 24 | (uint32_t)rr << 16 | (uint32_t)gg << 8 | bb;
}

static int
draw_child(struct sprite *s, const struct srt *srt, const struct sprite_trans * ts) {
	++s->time;

	struct sprite_trans temp;
	struct matrix temp_matrix;
	struct sprite_trans *t = sprite_trans_mul((struct sprite_trans*)(&s->t), (struct sprite_trans*)ts, &temp, &temp_matrix);

	switch (s->type) {
	case TYPE_PICTURE:
		sprite_drawquad(s, s->s.pic, srt, t);
		return 0;
	case TYPE_LABEL:
		ej_gtxt_draw(s, srt, t);
		return 0;
	case TYPE_ANCHOR:
		anchor_update(s, srt, t);
		return 0;
	case TYPE_ANIMATION:
		break;
	case TYPE_PANNEL:
		if (s->data.scissor) {
			// enable scissor
			set_scissor(s->s.pannel, srt, t);
			return 1;
		} else {
			return 0;
		}
	case TYPE_PARTICLE3D:
		{
			assert(0);
			return 0;
		}
		break;
	case TYPE_PARTICLE2D:
		{
//			sprite_draw_p2d(s, srt, t);
			ej_ps_draw(s, srt, t);
			return 0;
		}
		break;
	case TYPE_P3D_SPR:
		{
			ej_ps_draw(s, srt, t);
			return 0;
		}
		break;
	case TYPE_SHAPE:
		{
//			ej_rvg_draw(s->s.shape, srt, t);
			return 0;
		}
		break;
	default:
		// todo : invalid type
		return 0;
	}
	// draw animation
	int frame = get_frame(s);
	if (frame < 0) {
		return 0;
	}
	struct pack_animation *ani = s->s.ani;
	struct pack_frame * pf = &ani->frame[frame];
	int i;
	int scissor = 0;
	for (i=0;i<pf->n;i++) {
		struct pack_part *pp = &pf->part[i];
		int index = pp->component_id;
		struct sprite * child = s->data.children[index];
		if (child == NULL || (child->flags & SPRFLAG_INVISIBLE)) {
			continue;
		}
		struct sprite_trans temp2;
		struct matrix temp_matrix2;
		struct sprite_trans *ct = sprite_trans_mul(&pp->t, t, &temp2, &temp_matrix2);
		scissor += draw_child(child, srt, ct);
	}
	for (i=0;i<scissor;i++) {
		scissor_pop();
	}
	return 0;
}

bool
sprite_child_visible(struct sprite *s, const char * childname) {
	struct pack_animation *ani = s->s.ani;
	int frame = get_frame(s);
	if (frame < 0) {
		return false;
	}
	struct pack_frame * pf = &ani->frame[frame];
	int i;
	for (i=0;i<pf->n;i++) {
		struct pack_part *pp = &pf->part[i];
		int index = pp->component_id;
		struct sprite * child = s->data.children[index];
		if (child->name && strcmp(childname, child->name) == 0) {
			return true;
		}
	}
	return false;
}

void
sprite_draw(struct sprite *s, const struct srt *srt) {
	if ((s->flags & SPRFLAG_INVISIBLE) == 0) {
		draw_child(s, srt, NULL);
	}
}

void 
sprite_draw_with_trans(struct sprite* s, const struct srt* srt, const struct sprite_trans* ts) {
	if ((s->flags & SPRFLAG_INVISIBLE) == 0) {
		draw_child(s, srt, ts);
	}
}

void
sprite_draw_as_child(struct sprite *s, const struct srt *srt, struct matrix *mat, uint32_t color) {
	if ((s->flags & SPRFLAG_INVISIBLE) == 0) {
		struct sprite_trans st;
		st.mat = mat;
		st.color = color;
		st.additive = 0;
		st.rmap = 0xff0000ff;
		st.gmap = 0x00ff00ff;
		st.bmap = 0x0000ffff;
		st.program = /*PROGRAM_DEFAULT*/0;
		draw_child(s, srt, &st);
	}
}

int
sprite_pos(struct sprite *s, struct srt *srt, struct matrix *m, int pos[2]) {
	struct matrix temp;
	struct matrix *t = mat_mul(s->t.mat, m, &temp);
	matrix_srt(t, srt);
	switch (s->type) {
	case TYPE_PICTURE:
		picture_pos(t->m, s->s.pic, pos);
		return 0;
	case TYPE_LABEL:
		label_pos(t->m, s->s.label, pos);
		return 0;
	case TYPE_ANIMATION:
	case TYPE_PANNEL:
		pos[0] = t->m[4] / SCREEN_SCALE;
		pos[1] = t->m[5] / SCREEN_SCALE;
		return 0;
	case TYPE_PARTICLE3D: case TYPE_PARTICLE2D: case TYPE_P3D_SPR:
		return 0;
	default:
		return 1;
	}

}

void
sprite_matrix(struct sprite * self, struct matrix *mat) {
	struct sprite * parent = self->parent;
	if (parent) {
		assert(parent->type == TYPE_ANIMATION);
		sprite_matrix(parent, mat);
		struct matrix tmp;
		struct matrix * parent_mat = parent->t.mat;

		struct matrix * child_mat = NULL;
		struct pack_animation *ani = parent->s.ani;
		int frame = get_frame(parent);
		if (frame < 0) {
			return;
		}
		struct pack_frame * pf = &ani->frame[frame];
		int i;
		for (i=0;i<pf->n;i++) {
			struct pack_part *pp = &pf->part[i];
			int index = pp->component_id;
			struct sprite * child = parent->data.children[index];
			if (child == self) {
				child_mat = pp->t.mat;
				break;
			}
		}

		if (parent_mat == NULL && child_mat == NULL)
			return;

		if (parent_mat) {
			matrix_mul(&tmp, parent_mat, mat);
		} else {
			tmp = *mat;
		}

		if (child_mat) {
			matrix_mul(mat, child_mat, &tmp);
		} else {
			*mat = tmp;
		}
	} else {
		matrix_identity(mat);
	}
}

// aabb

static void
poly_aabb(int n, const int32_t * point, struct srt *srt, struct matrix *ts, int aabb[4]) {
	struct matrix mat;
	if (ts == NULL) {
		matrix_identity(&mat);
	} else {
		mat = *ts;
	}
	matrix_srt(&mat, srt);
	int *m = mat.m;

	int i;
	for (i=0;i<n;i++) {
		int x = point[i*2];
		int y = point[i*2+1];

		int xx = (x * m[0] + y * m[2]) / 1024 + m[4];
		int yy = (x * m[1] + y * m[3]) / 1024 + m[5];

		if (xx < aabb[0])
			aabb[0] = xx;
		if (xx > aabb[2])
			aabb[2] = xx;
		if (yy < aabb[1])
			aabb[1] = yy;
		if (yy > aabb[3])
			aabb[3] = yy;
	}
}

static inline void
quad_aabb(struct pack_picture * pic, struct srt *srt, struct matrix *ts, int aabb[4]) {
	int i;
	for (i=0;i<pic->n;i++) {
		poly_aabb(4, pic->rect[i].screen_coord, srt, ts, aabb);
	}
}

static inline void
polygon_aabb(struct pack_polygon * polygon, struct srt *srt, struct matrix *ts, int aabb[4]) {
	int i;
	for (i=0;i<polygon->n;i++) {
		struct pack_poly * poly = &polygon->poly[i];
		poly_aabb(poly->n, poly->screen_coord, srt, ts, aabb);
	}
}

static inline void
label_aabb(struct pack_label *label, struct srt *srt, struct matrix *ts, int aabb[4]) {
	int32_t point[] = {
		0,0,
		label->width * SCREEN_SCALE, 0,
		0, label->height * SCREEN_SCALE,
		label->width * SCREEN_SCALE, label->height * SCREEN_SCALE,
	};
	poly_aabb(4, point, srt, ts, aabb);
}

static inline void
panel_aabb(struct pack_pannel *panel, struct srt *srt, struct matrix *ts, int aabb[4]) {
	int32_t point[] = {
		0,0,
		panel->width * SCREEN_SCALE, 0,
		0, panel->height * SCREEN_SCALE,
		panel->width * SCREEN_SCALE, panel->height * SCREEN_SCALE,
	};
	poly_aabb(4, point, srt, ts, aabb);
}

static int
child_aabb(struct sprite *s, struct srt *srt, struct matrix * mat, int aabb[4]) {
	struct matrix temp;
	struct matrix *t = mat_mul(s->t.mat, mat, &temp);
	switch (s->type) {
	case TYPE_PICTURE:
		quad_aabb(s->s.pic, srt, t, aabb);
		return 0;
	case TYPE_POLYGON:
		polygon_aabb(s->s.poly, srt, t, aabb);
		return 0;
	case TYPE_LABEL:
		label_aabb(s->s.label, srt, t, aabb);
		return 0;
	case TYPE_ANIMATION:
		break;
	case TYPE_PANNEL:
		panel_aabb(s->s.pannel, srt, t, aabb);
		return s->data.scissor;
	default:
		// todo : invalid type
		return 0;
	}
	// draw animation
	struct pack_animation *ani = s->s.ani;
	int frame = get_frame(s);
	if (frame < 0) {
		return 0;
	}
	struct pack_frame * pf = &ani->frame[frame];
	int i;
	for (i=0;i<pf->n;i++) {
		struct pack_part *pp = &pf->part[i];
		int index = pp->component_id;
		struct sprite * child = s->data.children[index];
		if (child == NULL || (child->flags & SPRFLAG_INVISIBLE)) {
			continue;
		}
		struct matrix temp2;
		struct matrix *ct = mat_mul(pp->t.mat, t, &temp2);
		if (child_aabb(child, srt, ct, aabb))
			break;
	}
	return 0;
}

void
sprite_aabb(struct sprite *s, struct srt *srt, bool world_aabb, int aabb[4]) {
	int i;
	if ((s->flags & SPRFLAG_INVISIBLE) == 0) {
		struct matrix tmp;
		if (world_aabb) {
			sprite_matrix(s, &tmp);
		} else {
			matrix_identity(&tmp);
		}
		aabb[0] = INT_MAX;
		aabb[1] = INT_MAX;
		aabb[2] = INT_MIN;
		aabb[3] = INT_MIN;
		child_aabb(s,srt,&tmp,aabb);
		for (i=0;i<4;i++)
			aabb[i] /= SCREEN_SCALE;
	} else {
		for (i=0;i<4;i++)
			aabb[i] = 0;
	}
}

// test

static int
test_quad(struct pack_picture * pic, int x, int y) {
	int p;
	for (p=0;p<pic->n;p++) {
		struct pack_quad *pq = &pic->rect[p];
		int maxx,maxy,minx,miny;
		minx= maxx = pq->screen_coord[0];
		miny= maxy = pq->screen_coord[1];
		int i;
		for (i=2;i<8;i+=2) {
			int x = pq->screen_coord[i];
			int y = pq->screen_coord[i+1];
			if (x<minx)
				minx = x;
			else if (x>maxx)
				maxx = x;
			if (y<miny)
				miny = y;
			else if (y>maxy)
				maxy = y;
		}
		if (x>=minx && x<=maxx && y>=miny && y<=maxy)
			return 1;
	}
	return 0;
}

static int
test_polygon(struct pack_polygon * poly,  int x, int y) {
	int p;
	for (p=0;p<poly->n;p++) {
		struct pack_poly *pp = &poly->poly[p];
		int maxx,maxy,minx,miny;
		minx= maxx = pp->screen_coord[0];
		miny= maxy = pp->screen_coord[1];
		int i;
		for (i=1;i<pp->n;i++) {
			int x = pp->screen_coord[i*2+0];
			int y = pp->screen_coord[i*2+1];
			if (x<minx)
				minx = x;
			else if (x>maxx)
				maxx = x;
			if (y<miny)
				miny = y;
			else if (y>maxy)
				maxy = y;
		}
		if (x>=minx && x<=maxx && y>=miny && y<=maxy) {
			return 1;
		}
	}
	return 0;
}

static int
test_label(struct pack_label *label, int x, int y) {
	int hw = label->width / 2;
	int hh = label->height / 2;
	x /= SCREEN_SCALE;
	y /= SCREEN_SCALE;
	return x>=-hw && x<hw && y>=-hh && y<hh;
}

static int
test_pannel(struct pack_pannel *pannel, int x, int y) {
	x /= SCREEN_SCALE;
	y /= SCREEN_SCALE;
	return x>=0 && x<pannel->width && y>=0 && y<pannel->height;
}

static int test_child(struct sprite *s, struct srt *srt, struct matrix * ts, int x, int y, struct sprite ** touch);

static int
check_child(struct sprite *s, struct srt *srt, struct matrix * t, struct pack_frame * pf, int i, int x, int y, struct sprite ** touch) {
	struct pack_part *pp = &pf->part[i];
	int index = pp->component_id;
	struct sprite * child = s->data.children[index];
	if (child == NULL || (child->flags & SPRFLAG_INVISIBLE)) {
		return 0;
	}
	struct matrix temp2;
	struct matrix *ct = mat_mul(pp->t.mat, t, &temp2);
	struct sprite *tmp = NULL;
	int testin = test_child(child, srt, ct, x, y, &tmp);
	if (testin) {
		// if child capture message, return it
		*touch = tmp;
		return 1;
	}
	if (tmp) {
		// if child not capture message, but grandson (tmp) capture it, mark it
		*touch = tmp;
	}
	return 0;
}

/*
	return 1 : test succ
		0 : test failed, but *touch capture the message
 */
static int
test_animation(struct sprite *s, struct srt *srt, struct matrix * t, int x, int y, struct sprite ** touch) {
	struct pack_animation *ani = s->s.ani;
	int frame = get_frame(s);
	if (frame < 0) {
		return 0;
	}
	struct pack_frame * pf = &ani->frame[frame];
	int start = pf->n-1;
	do {
		int scissor = -1;
		int i;
		// find scissor and check it first
		for (i=start;i>=0;i--) {
			struct pack_part *pp = &pf->part[i];
			int index = pp->component_id;
			struct sprite * c = s->data.children[index];
			if (c == NULL || (c->flags & SPRFLAG_INVISIBLE)) {
				continue;
			}
			if (c->type == TYPE_PANNEL && c->data.scissor) {
				scissor = i;
				break;
			}

		}
		if (scissor >=0) {
			struct sprite *tmp = NULL;
			check_child(s, srt, t, pf, scissor, x, y, &tmp);
			if (tmp == NULL) {
				start = scissor - 1;
				continue;
			}
		} else {
			scissor = 0;
		}
		for (i=start;i>=scissor;i--) {
			int hit = check_child(s, srt, t,  pf, i, x, y, touch);
			if (hit)
				return 1;
		}
		start = scissor - 1;
	} while(start>=0);
	return 0;
}

static int
test_child(struct sprite *s, struct srt *srt, struct matrix * ts, int x, int y, struct sprite ** touch) {
	struct matrix temp;
	struct matrix *t = mat_mul(s->t.mat, ts, &temp);
	if (s->type == TYPE_ANIMATION) {
		struct sprite *tmp = NULL;
		int testin = test_animation(s , srt, t, x,y, &tmp);
		if (testin) {
			*touch = tmp;
			return 1;
		} else if (tmp) {
			if (s->flags & SPRFLAG_MESSAGE) {
				*touch = s;
				return 1;
			} else {
				*touch = tmp;
				return 0;
			}
		}
	}
	struct matrix mat;
	if (t == NULL) {
		matrix_identity(&mat);
	} else {
		mat = *t;
	}
	matrix_srt(&mat, srt);
	struct matrix imat;
	if (matrix_inverse(&mat, &imat)) {
		// invalid matrix
		*touch = NULL;
		return 0;
	}
	int *m = imat.m;

	int xx = (x * m[0] + y * m[2]) / 1024 + m[4];
	int yy = (x * m[1] + y * m[3]) / 1024 + m[5];

	int testin;
	struct sprite * tmp = s;
	switch (s->type) {
	case TYPE_PICTURE:
		testin = test_quad(s->s.pic, xx, yy);
		break;
	case TYPE_POLYGON:
		testin = test_polygon(s->s.poly, xx, yy);
		break;
	case TYPE_LABEL:
		testin = test_label(s->s.label, xx, yy);
		if (testin) {
			*touch = ej_gtxt_point_query(s, xx, yy, srt);
		}
		break;
	case TYPE_PANNEL:
		testin = test_pannel(s->s.pannel, xx, yy);
		break;
	case TYPE_ANCHOR:
		*touch = NULL;
		return 0;
	default:
		// todo : invalid type
		*touch = NULL;
		return 0;
	}

	if (testin) {
		*touch = tmp;
		return s->flags & SPRFLAG_MESSAGE;
	} else {
		*touch = NULL;
		return 0;
	}
}

struct sprite *
sprite_test(struct sprite *s, struct srt *srt, int x, int y) {
	struct sprite *tmp = NULL;
	int testin = test_child(s, srt, NULL, x, y, &tmp);
	if (testin) {
		return tmp;
	}
	if (tmp) {
		return s;
	}
	return NULL;
}

static inline int
propagate_frame(struct sprite *s, int i, bool force_child) {
	if (s->type == TYPE_PARTICLE3D || s->type == TYPE_PARTICLE2D || s->type == TYPE_P3D_SPR) {
		return 1;
	}

	struct sprite *child = s->data.children[i];
	if (child == NULL || 
		(child->type != TYPE_ANIMATION && child->type != TYPE_PARTICLE3D && child->type != TYPE_PARTICLE2D && child->type != TYPE_P3D_SPR)) {
		return 0;
	}
	if (child->flags & SPRFLAG_FORCE_INHERIT_FRAME) {
		return 1;
	}
	struct pack_animation * ani = s->s.ani;
	if (ani->component[i].id == ANCHOR_ID) {
		return 0;
	}
	if (force_child) {
		return 1;
	}
	if (ani->component[i].name == 0) {
		return 1;
	}

	return 0;
}

int
sprite_setframe(struct sprite *s, int frame, bool force_child) {
	if (s == NULL || (s->type != TYPE_ANIMATION && s->type != TYPE_PARTICLE3D && s->type != TYPE_PARTICLE2D && s->type != TYPE_P3D_SPR))
		return 0;

	s->frame = frame;

	int total_frame = s->total_frame;
	if (s->type == TYPE_ANIMATION) {
		int i;
		struct pack_animation * ani = s->s.ani;
		for (i=0;i<ani->component_number;i++) {
			if (propagate_frame(s, i, force_child)) {
				if (s->flags & SPRFLAG_RANDOM_CHILD_BASE_FRAME) {
					frame = frame + ((i * 134775813) % 307);
				}
				int t = sprite_setframe(s->data.children[i], frame, force_child);
				if (t > total_frame) {
					total_frame = t;
				}
			}
		}
	} else if (s->type == TYPE_PARTICLE3D || s->type == TYPE_PARTICLE2D) {
		int num;
		if (s->type == TYPE_PARTICLE3D) {
			num = s->s.p3d->symbol_count;
		} else {
			num = s->s.p2d->symbol_count;
		}
		for (int i = 0; i < num; ++i) {
			if (propagate_frame(s, i, force_child)) {
				int t = sprite_setframe(s->data.children[i], frame, force_child);
				if (t > total_frame) {
					total_frame = t;
				}
			}
		}
	} else if (s->type == TYPE_P3D_SPR) {
		if (propagate_frame(s, 0, force_child)) {
			int t = sprite_setframe(s->data.children[0], frame, force_child);
			if (t > total_frame) {
				total_frame = t;
			}
		}
	}
	return total_frame;
}