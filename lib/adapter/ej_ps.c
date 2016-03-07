#include "ej_ps.h"
#include "sprite.h"
#include "spritepack.h"

#include <dtex_package.h>
#include <ps.h>

#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#define DEGREES_TO_RADIANS(__ANGLE__) ((__ANGLE__) * 0.01745329252f) // PI / 180
#define RADIANS_TO_DEGREES(__ANGLE__) ((__ANGLE__) * 57.29577951f) // PI * 180

static lua_State* L;

struct state {
	float time;
	bool run;
};

struct render_params {
	struct sprite* spr;

	struct srt srt;

	struct matrix mat;
	uint32_t color;
	uint32_t additive;
	uint32_t rmap, gmap, bmap;
	int program;
};

static struct state STATE;

void 
ej_ps_set_lua_state(lua_State* _L) {
	L = _L;
}

int 
ej_sprite_sprite_ref(int idx) {
	lua_getfield(L, LUA_REGISTRYINDEX, "ps");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		lua_newtable(L);
	}

	lua_pushvalue(L, idx);
	int ref_id = luaL_ref(L, -2);
	lua_setfield(L, LUA_REGISTRYINDEX, "ps");
	return ref_id;
}

void 
_sprite_unref(int ref_id) {
	lua_getfield(L, LUA_REGISTRYINDEX, "ps");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
	} else {
		luaL_unref(L, -1, ref_id);
		lua_pop(L, 1);
	}
}

static void 
p3d_render_func(void* symbol, float* mat, float x, float y, float angle, float scale, 
                struct ps_color4f* mul_col, struct ps_color4f* add_col, const void* ud) {
	struct render_params* params = (struct render_params*)ud;
	if (!params->spr) {
		return;
	}

	assert(params->spr->type == TYPE_P3D_SPR);

	int idx = (uint32_t)symbol & 0xffff;
	struct sprite* p3d_sym = params->spr->data.children[0];
	struct sprite* child = p3d_sym->data.children[idx];
	child->t.color = color4f(mul_col);
	child->t.additive = color4f(add_col);

	struct srt c_srt;
	c_srt.offx = x*SCREEN_SCALE;
	c_srt.offy = -y*SCREEN_SCALE;
	c_srt.scalex = scale*1024;
	c_srt.scaley = scale*1024;

	int deg = RADIANS_TO_DEGREES(-angle);
	deg = deg - floor(deg / 360.0f) * 360;
	c_srt.rot = deg * (EJMAT_R_FACTOR / 360.0);

	struct matrix mt;
	matrix_identity(&mt);
	matrix_srt(&mt, &c_srt);
	child->t.mat = &mt;

	struct sprite_trans st;
	st.color = params->color;
	st.additive = params->additive;
	st.rmap = params->rmap;
	st.gmap = params->gmap;
	st.bmap = params->bmap;
	st.program = params->program;

	if (params->spr->data_ext.p3d->local_mode_draw) {
		st.mat = &params->mat;
		sprite_draw_with_trans(child, &params->srt, &st);
	} else {
		struct matrix mt;
 		for (int i = 0; i < 6; ++i) {
 			mt.m[i] = mat[i];
 		}
		st.mat = &mt;
		sprite_draw_with_trans(child, &params->srt, &st);
	}
}

static void
p2d_render_func(void* symbol, float* m, float x, float y, float angle, float scale, 
                struct ps_color4f* mul_col, struct ps_color4f* add_col, const void* ud) {
	struct render_params* params = (struct render_params*)ud;

	int idx = (*(int*)(symbol)) & 0xffff;
	struct sprite* child = params->spr->data.children[idx];
	child->t.color = color4f(mul_col);
	child->t.additive = color4f(add_col);

	struct srt c_srt;
	c_srt.offx = x * SCREEN_SCALE;
	c_srt.offy = - y * SCREEN_SCALE;
	c_srt.scalex = scale * 1024;
	c_srt.scaley = scale * 1024;

	int deg = RADIANS_TO_DEGREES(-angle);
	deg = deg - floor(deg / 360.0f) * 360;
	c_srt.rot = deg * (EJMAT_R_FACTOR / 360.0);

	struct matrix mat;
	matrix_identity(&mat);
	matrix_srt(&mat, &c_srt);
	child->t.mat = &mat;

	struct sprite_trans st;
	st.mat = &params->mat;
	st.color = params->color;
	st.additive = params->additive;
	st.rmap = params->rmap;
	st.gmap = params->gmap;
	st.bmap = params->bmap;
	st.program = params->program;
	sprite_draw_with_trans(child, &params->srt, &st);
}

static void
update_srt_func(void* params, float x, float y, float scale) {
	struct render_params* rp = (struct render_params*)params;
	rp->srt.offx = x * SCREEN_SCALE;
	rp->srt.offy = y * SCREEN_SCALE;
	rp->srt.scalex = rp->srt.scaley = scale * 1024;
	rp->srt.rot = 0;
}

static void
remove_func(struct p3d_sprite* spr) {
	if (spr->ref_id == 0) {
		return;
	}

	_sprite_unref(spr->ref_id);	
}

static void
create_draw_params_func(struct p3d_sprite* spr) {
	void* rp = malloc(sizeof(struct render_params));
	memset(rp, 0, sizeof(struct render_params));
	spr->draw_params = rp;
}

void 
release_draw_params_func(struct p3d_sprite* spr) {
	struct render_params* rp = (struct render_params*)(spr->draw_params);
	free(rp);
	spr->draw_params = NULL;
}

void 
ej_ps_init() {
	STATE.time = 0;
	STATE.run = true;

	p3d_init();
	p3d_regist_cb(p3d_render_func, NULL, NULL);
	p3d_buffer_init(update_srt_func, remove_func);
	p3d_sprite_init(create_draw_params_func, release_draw_params_func);

	p2d_init();
	p2d_regist_cb(p2d_render_func);
}

static inline void 
_update_time(float dt) {
	if (STATE.run) {
		STATE.time += dt;
	}
}

static inline float 
_get_time() {
	return STATE.time;
}

void 
ej_ps_pause() {
	STATE.run = false;
}

void 
ej_ps_resume() {
	STATE.run = true;
}

static void
_draw_p2d(struct sprite* spr, const struct srt* srt, const struct sprite_trans* trans) {
	struct p2d_emitter* et = NULL;
	if (spr->parent && spr->parent->data_ext.p2d && (intptr_t)(spr->parent->data_ext.p2d) != ULONG_MAX) {
		et = spr->parent->data_ext.p2d;
	} else {
		et = spr->data_ext.p2d;
	}
	if (!et) {
		return;
	}
	if (et->time == 0) {
		et->time = _get_time();
	} else {
		// update
		float time = _get_time();
		if (et->time < time) {
			float dt = time - et->time;
			if (dt > 0.1f) {
				dt = 1 / 30.0f;
				et->time = time - dt;
			}

			// matrix
			struct matrix tmp;
			if (trans->mat == NULL) {
				matrix_identity(&tmp);
			} else {
				tmp = *trans->mat;
			}
			float mat[6];
			for (int i = 0; i < 6; ++i) {
				mat[i] = tmp.m[i];
			}

			p2d_emitter_update(et, dt, mat);
			et->time = time;
		}

		struct render_params params;
		params.spr = spr;
		params.srt = *srt;
		if (trans->mat) {
			params.mat = *trans->mat;
		} else {
			memset(trans->mat, 0, sizeof(trans->mat));
			trans->mat->m[0] = trans->mat->m[3] = 1024;
		}
		params.color = trans->color;
		params.additive = trans->additive;
		params.rmap = trans->rmap;
		params.gmap = trans->gmap;
		params.bmap = trans->bmap;
		params.program = trans->program;

		p2d_emitter_draw(et, &params);
	}
}

static void
_create_p3d_spr(struct sprite* spr) {
	assert(spr->type == TYPE_P3D_SPR);

	struct sprite_pack* ej_pkg = spr->pkg->ej_pkg;
	struct pack_p3d_spr* spr_cfg = (struct pack_p3d_spr*)ej_pkg->data[spr->id];
	struct p3d_emitter_cfg* cfg = (struct p3d_emitter_cfg*)ej_pkg->data[spr_cfg->id];

	struct p3d_emitter* et = NULL;
	// 		if (spr_cfg->reuse) {
	// 			et = new_child->data_ext.p3d->et;
	// 		} else {
	// 			et = p3d_emitter_create(cfg);
	// 		}
	et = p3d_emitter_create(cfg);

	assert(et);
	et->active = true;
	et->loop = spr_cfg->loop;

	struct p3d_sprite* p3d = p3d_sprite_create();
	assert(p3d);
	p3d->et = et;

	p3d->local_mode_draw = spr_cfg->local;
	memset(p3d->mat, 0, sizeof(p3d->mat));
	spr->data_ext.p3d = p3d;
	p3d->ptr_self = &spr->data_ext.p3d;
}

static void
_draw_p3d_spr(struct sprite* spr, const struct srt* srt, const struct sprite_trans* trans) {
	if (spr->s.p3d_spr->alone) {
		// not loop, clear at buffer
		if (!spr->data_ext.p3d) {
			_create_p3d_spr(spr);
			spr->data_ext.p3d->et->loop = false;
			spr->data_ext.p3d->et->active = true;
			spr->data_ext.p3d->local_mode_draw = true;
		}

		struct p3d_sprite* p3d = spr->data_ext.p3d;
		struct render_params* params = NULL;
		if (!p3d->draw_params) {
			p3d_buffer_insert(p3d);
			params = p3d->draw_params;
			params->spr = spr;
		} else {
			params = p3d->draw_params;
			assert(params->spr);
		}
		params->srt = *srt;

		if (trans->mat) {
			params->mat = *trans->mat;
		} else {
			memset(trans->mat, 0, sizeof(trans->mat));
			trans->mat->m[0] = trans->mat->m[3] = 1024;
		}
		params->color = trans->color;
		params->additive = trans->additive;
		params->rmap = trans->rmap;
		params->gmap = trans->gmap;
		params->bmap = trans->bmap;
		params->program = trans->program;
		if (trans->mat) {
			for (int i = 0; i < 6; ++i) {
				p3d->mat[i] = trans->mat->m[i];
			}
		}
		return;
	}

	if (!spr->data_ext.p3d || !spr->data_ext.p3d->et) {
		return;
	}
	struct p3d_emitter* et = spr->data_ext.p3d->et;

	if (et->time == 0) {
		et->time = _get_time();
	} else {
		// update
		float time = _get_time();
		if (et->time < time) {
			float dt = time - et->time;
			if (dt > 0.1f) {
				dt = 1 / 30.0f;
				et->time = time - dt;
			}

			// matrix
			struct matrix tmp;
			if (trans->mat == NULL) {
				matrix_identity(&tmp);
			} else {
				tmp = *trans->mat;
			}
			float mat[6];
			for (int i = 0; i < 6; ++i) {
				mat[i] = tmp.m[i];
			}

			p3d_emitter_update(et, dt, mat);
			et->time = time;
		}

		// draw
		struct render_params params;
		params.spr = spr;
		params.srt = *srt;
		if (trans->mat) {
			params.mat = *trans->mat;
		} else {
			memset(trans->mat, 0, sizeof(trans->mat));
			trans->mat->m[0] = trans->mat->m[3] = 1024;
		}
		params.color = trans->color;
		params.additive = trans->additive;
		params.rmap = trans->rmap;
		params.gmap = trans->gmap;
		params.bmap = trans->bmap;
		params.program = trans->program;

		p3d_emitter_draw(et, &params);
	}
}

void 
ej_ps_update(float dt) {
	_update_time(dt);
	p3d_buffer_update(_get_time());
}

void 
ej_ps_draw(struct sprite* spr, const struct srt* srt, const struct sprite_trans* trans) {
	if (spr->type == TYPE_P3D_SPR) {
		_draw_p3d_spr(spr, srt, trans);
	} else if (spr->type == TYPE_PARTICLE2D) {
		_draw_p2d(spr, srt, trans);
	}
}

void 
ej_ps_mount(struct sprite* parent, struct sprite* old_child, struct sprite* new_child) {
	// bind p3d to p3d_spr, on create p3d_spr
	if (parent->type == TYPE_P3D_SPR) {
		assert(new_child->type == TYPE_PARTICLE3D
			&& parent->data.children[0]
		&& parent->data.children[0] == new_child);
		_create_p3d_spr(parent);

		if (parent->s.p3d_spr->alone) {
			parent->data_ext.p3d->ref_id = ej_sprite_sprite_ref(3);
		}
	}

// 	// bind 
//  	if (child && child->type == TYPE_P3D_SPR) {
//  
//  	}

//  	if (oldc->type == TYPE_P3D_SPR) {
//  		struct pack_p3d_spr* cfg = oldc->s.p3d_spr;
//  		struct p3d_sprite* spr = oldc->data_ext.p3d;
//  		if (!m_alone) {
//  			p3d_emitter_release(m_spr->et);
//  		} else {
//  			if (m_spr->et->loop) {
//  				p3d_emitter_release(m_spr->et);
//  				p3d_buffer_remove(m_spr);
//  			}
//  		}
//  	}
}

void 
ej_ps_sprite_gc(struct sprite* spr) {
	assert(spr->type == TYPE_P3D_SPR);
	// removed form buffer
	if (!spr->data_ext.p3d) {
		return;
	}
	// not alone
	if (!spr->s.p3d_spr->alone) {
		return;
	}

	struct p3d_sprite* p3d = spr->data_ext.p3d;
	assert(!p3d_buffer_query(p3d));
	if (p3d->et) {
		p3d_sprite_release(p3d);
	}
	spr->data_ext.p3d = NULL;
}