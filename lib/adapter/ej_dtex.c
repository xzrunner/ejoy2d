#include "ej_dtex.h"
#include "ej_shaderlab.h"

#include <dtex.h>
#include <shaderlab.h>

#include <assert.h>

static void
_program(int n) {
	switch (n) {
	case DTEX_PROGRAM_NULL:
		assert(0);
//		shader_program(PROGRAM_DEFAULT);
		break;
	case DTEX_PROGRAM_NORMAL:
		ej_sl_sprite();
		break;
	default:
		assert(0);
	}
}

static void
_blend(int mode) {
	assert(0);
}

static void 
_set_texture(int id) {
	sl_shader_set_texture(id, 0);
}

static int
_get_texture() {
	return sl_shader_get_texture();
}

static void
_set_target(int id) {
	sl_shader_set_target(id);
}

static int
_get_target() {
	return sl_shader_get_target();
}

static void
_draw_begin() {

}

static void
_draw(const float vb[16], int texid) {
	float positions[8], texcoords[8];

	positions[0] = vb[0] + 1; positions[1] = vb[1] - 1; 
	positions[2] = vb[4] + 1; positions[3] = vb[5] - 1; 
	positions[4] = vb[8] + 1; positions[5] = vb[9] - 1; 
	positions[6] = vb[12]+ 1; positions[7] = vb[13]- 1; 

// 	texcoords[0] = vb[2] * 0xffff; texcoords[1] = vb[3] * 0xffff;
// 	texcoords[2] = vb[6] * 0xffff; texcoords[3] = vb[7] * 0xffff;
// 	texcoords[4] = vb[10]* 0xffff; texcoords[5] = vb[11]* 0xffff;
// 	texcoords[6] = vb[14]* 0xffff; texcoords[7] = vb[15]* 0xffff;
	texcoords[0] = vb[2];  texcoords[1] = vb[3];
	texcoords[2] = vb[6];  texcoords[3] = vb[7];
	texcoords[4] = vb[10]; texcoords[5] = vb[11];
	texcoords[6] = vb[14]; texcoords[7] = vb[15];

	sl_sprite_set_color(0xffffffff, 0);
	sl_sprite_set_map_color(0xff0000ff, 0x00ff00ff, 0x0000ffff);

	sl_sprite_draw(positions, texcoords, texid);
}

static void
_draw_end() {
	sl_shader_flush();
}

static void
_draw_flush() {
	sl_shader_flush();
}


static int 
_texture_create(int type, int width, int height, const void* data, int channel, unsigned int id) {
	enum TEXTURE_FORMAT t = TEXTURE_RGBA8;
	switch (type) {
	case DTEX_TF_RGBA8:
		t = TEXTURE_RGBA8;
		break;
	case DTEX_TF_PVR4:
		t = TEXTURE_PVR4;
        break;
	default:
		assert(0);
	}

	struct render* r = sl_shader_get_render();
	int texid = render_texture_create(r, width, height, t, TEXTURE_2D, 0);
	render_texture_update(r, texid, width, height, data, 0, 0);
	return texid;
}

static void 
_texture_release(int id) {
	render_release(sl_shader_get_render(), TEXTURE, id);
}

static void
_texture_update(const void* pixels, int x, int y, int w, int h, unsigned int id) {
	render_texture_subupdate(sl_shader_get_render(), id, pixels, x, y, w, h);
}

static int
_texture_id(int id) {
	return render_get_texture_gl_id(sl_shader_get_render(), id);
}

void 
ej_dtex_init() {
	dtex_shader_init(&_program, &_blend, &_set_texture, &_get_texture, 
		&_set_target, &_get_target, &_draw_begin, &_draw, &_draw_end, &_draw_flush);

	dtex_gl_texture_init(&_texture_create, &_texture_release, &_texture_update, &_texture_id);
}