#include "ej_shaderlab.h"

#include <shaderlab.h>

#include <stddef.h>

enum SHADER_TYPE {
	ST_NULL = 0,
	ST_SPRITE,
	ST_SHAPE
};

struct state {
	enum SHADER_TYPE type;

	void (*commit_func)();
	void (*unbind_func)();

};

static struct state S;

void 
ej_sl_create() {
	S.type = ST_NULL;
	S.commit_func = NULL;
	S.unbind_func = NULL;

	sl_shader_mgr_create();

	sl_sprite_load();
	sl_shape_load();
}

void 
ej_sl_release() {
	sl_shader_mgr_release();
}

void 
ej_sl_sprite() {
	if (S.type == ST_SPRITE) {
		return;
	}

	if (S.commit_func) {
		S.commit_func();
	}
	if (S.unbind_func) {
		S.unbind_func();
	}
	sl_sprite_bind();

	S.type = ST_SPRITE;
	S.commit_func = sl_sprite_commit;
	S.unbind_func = sl_sprite_unbind;
}

void 
ej_sl_shape() {
	if (S.type == ST_SHAPE) {
		return;
	}
	
	if (S.commit_func) {
		S.commit_func();
	}
	if (S.unbind_func) {
		S.unbind_func();
	}
	sl_shape_bind();

	S.type = ST_SHAPE;
	S.commit_func = sl_shape_commit;
	S.unbind_func = sl_shape_unbind;
}

void 
ej_sl_on_size(int w, int h) {
	sl_sprite_projection(w, h);
	sl_sprite_modelview(-w*0.5f, h*0.5f, 1, 1);

	sl_shape_projection(w, h);
	sl_shape_modelview(-w*0.5f, h*0.5f, 1, 1);
}

void 
ej_sl_commit() {
	sl_sprite_commit();
	sl_shape_commit();
}