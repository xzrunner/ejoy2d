#include "spritepack.h"
#include "sprite.h"
#include "render.h"

#include "adapter/ej_gtxt.h"
#include "adapter/ej_ps.h"

#include <dtex.h>
#include <ps.h>

#include <lua.h>
#include <lauxlib.h>

#define SRT_X 1
#define SRT_Y 2
#define SRT_SX 3
#define SRT_SY 4
#define SRT_ROT 5
#define SRT_SCALE 6

static double
readkey(lua_State *L, int idx, int key, double def) {
	lua_pushvalue(L, lua_upvalueindex(key));
	lua_rawget(L, idx);
	double ret = luaL_optnumber(L, -1, def);
	lua_pop(L,1);
	return ret;
}

static void
fill_srt(lua_State *L, struct srt *srt, int idx) {
	if (lua_isnoneornil(L, idx)) {
		srt->offx = 0;
		srt->offy = 0;
		srt->rot = 0;
		srt->scalex = 1024;
		srt->scaley = 1024;
		return;
	}
	luaL_checktype(L,idx,LUA_TTABLE);
	double x = readkey(L, idx, SRT_X, 0);
	double y = readkey(L, idx, SRT_Y, 0);
	double scale = readkey(L, idx, SRT_SCALE, 0);
	double sx;
	double sy;
	double rot = readkey(L, idx, SRT_ROT, 0);
	if (scale > 0) {
		sx = sy = scale;
	} else {
		sx = readkey(L, idx, SRT_SX, 1);
		sy = readkey(L, idx, SRT_SY, 1);
	}
	srt->offx = x*SCREEN_SCALE;
	srt->offy = y*SCREEN_SCALE;
	srt->scalex = sx*1024;
	srt->scaley = sy*1024;
	srt->rot = rot * (EJMAT_R_FACTOR / 360.0);
}

static const char * srt_key[] = {
	"x",
	"y",
	"sx",
	"sy",
	"rot",
	"scale",
};

static void
update_anim_message(struct sprite * s, struct sprite_pack * pack, int parentid, int componentid, int frame) {
	struct pack_animation * ani = (struct pack_animation *)pack->data[parentid];
	if (frame < 0 || frame >= ani->frame_number) {
		return;
	}
	struct pack_frame pframe = ani->frame[frame];
	int i = 0;
	for (; i < pframe.n; i++) {
		if (pframe.part[i].component_id == componentid && pframe.part[i].touchable) {
			s->flags |= SPRFLAG_MESSAGE;
			return;
		}
	}
}

static void
update_p3d_message(struct sprite * s, struct sprite_pack * pack, int parentid, int componentid) {
	struct p3d_emitter_cfg* p3d_cfg = (struct p3d_emitter_cfg*)pack->data[parentid];
	if (componentid < 0 || componentid >= p3d_cfg->symbol_count) {
		return;
	}
	s->flags |= SPRFLAG_MESSAGE;
}

static void
update_p2d_message(struct sprite * s, struct sprite_pack * pack, int parentid, int componentid) {
	struct p2d_emitter_cfg* p2d_cfg = (struct p2d_emitter_cfg*)pack->data[parentid];
	if (componentid < 0 || componentid >= p2d_cfg->symbol_count) {
		return;
	}
	s->flags |= SPRFLAG_MESSAGE;
}

static struct sprite *
newanchor(lua_State *L) {
	int sz = sizeof(struct sprite) + sizeof(struct anchor_data);
	struct sprite * s = (struct sprite *)lua_newuserdata(L, sz);
	s->pkg = NULL;
	s->pkg_version = 0;
	s->parent = NULL;
	s->t.mat = NULL;
	s->t.color = 0xffffffff;
	s->t.additive = 0;
	s->t.rmap = 0xff0000ff;
	s->t.gmap = 0x00ff00ff;
	s->t.bmap = 0x0000ffff;
	s->t.program = /*PROGRAM_DEFAULT*/0;
	s->flags = SPRFLAG_INVISIBLE;  // anchor is invisible by default
	s->time = 0;
	s->name = NULL;
	s->id = ANCHOR_ID;
	s->type = TYPE_ANCHOR;
	s->data.anchor = (struct anchor_data *)(s+1);
	s->data.anchor->pic = NULL;
	s->s.mat = &s->data.anchor->mat;
	matrix_identity(s->s.mat);

	return s;
}

static struct sprite *
newsprite(lua_State *L, struct dtex_package* pkg, int id) {
	if (id == ANCHOR_ID) {
		return newanchor(L);
	}
	int sz = sprite_size(pkg, id);
	if (sz == 0) {
		return NULL;
	}
	struct sprite * s = (struct sprite *)lua_newuserdata(L, sz);
	sprite_init(s, pkg, id, sz);
	int i;
	for (i=0;;i++) {
		int childid = sprite_component(s, i);
		if (childid < 0)
			break;
		if (i==0) {
			lua_newtable(L);
			lua_pushvalue(L,-1);
			lua_setuservalue(L, -3);	// set uservalue for sprite
		}
		struct sprite *c = newsprite(L, pkg, childid);
		if (c) {
			c->name = sprite_childname(s, i);
			sprite_mount(s, i, c);
			if (s->type == TYPE_ANIMATION) {
				update_anim_message(c, pkg->ej_pkg, id, i, s->frame);
			} else if (s->type == TYPE_PARTICLE3D) {
				update_p3d_message(c, pkg->ej_pkg, id, i);
			} else if (s->type == TYPE_PARTICLE2D) {
				update_p2d_message(c, pkg->ej_pkg, id, i);
			} else if (s->type == TYPE_P3D_SPR) {
				if (i == 0) {
					c->flags |= SPRFLAG_MESSAGE;
				}
			}
			lua_rawseti(L, -2, i+1);
		}
	}
	if (i>0) {
		lua_pop(L,1);
	}
	return s;
}

/*
	userdata sprite_pack
	integer id

	ret: userdata sprite
 */
static int
lnew(lua_State *L) {
	struct dtex_package* pkg = (struct dtex_package*)lua_touserdata(L, 1);
	if (pkg == NULL) {
		return luaL_error(L, "Need a sprite pack");
	}
	int id = (int)luaL_checkinteger(L, 2);
	struct sprite * s = newsprite(L, pkg, id);
	if (s) {
		return 1;
	}
	return 0;
}

static void
_check_spr_pkg(lua_State *L, struct sprite* spr, int except_idx) {
	if (spr->pkg && !sprite_pkg_valid(spr, except_idx)) {
		luaL_error(L, "sprite pkg invalid!");
	}	
}

static struct sprite *
self(lua_State *L) {
	struct sprite * s = (struct sprite *)lua_touserdata(L, 1);
	if (s == NULL) {
		luaL_error(L, "Need sprite");
	}
	_check_spr_pkg(L, s, -1);
	return s;
}

static struct sprite *
self2(lua_State *L) {
	struct sprite * s = (struct sprite *)lua_touserdata(L, 1);
	if (s == NULL) {
		luaL_error(L, "Need sprite");
	}
	return s;
}

static void
get_reftable(lua_State *L, int index) {
	lua_getuservalue(L, index);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		lua_createtable(L, 0, 1);
		lua_pushvalue(L, -1);
		lua_setuservalue(L, index);
	}
}

static int
lgetframe(lua_State *L) {
	struct sprite * s = self(L);
	lua_pushinteger(L, s->frame);
	return 1;
}

static int
lsetframe(lua_State *L) {
	struct sprite * s = self(L);
	int frame = (int)luaL_checkinteger(L,2);
	sprite_setframe(s, frame, false);
	return 0;
}

static int
lsetaction(lua_State *L) {
	struct sprite * s = self(L);
	const char * name = lua_tostring(L,2);
	sprite_action(s, name);
	return 0;
}

static int
lgettotalframe(lua_State *L) {
	struct sprite *s = self(L);
	int f = s->total_frame;
	if (f<=0) {
		f = 0;
	}
	lua_pushinteger(L, f);
	return 1;
}

static int
lgetvisible(lua_State *L) {
	struct sprite *s = self(L);
	lua_pushboolean(L, (s->flags & SPRFLAG_INVISIBLE) == 0);
	return 1;
}

static int
lsetvisible(lua_State *L) {
	struct sprite *s = self(L);
	if (lua_toboolean(L, 2)) {
		s->flags &= ~SPRFLAG_INVISIBLE;
	} else {
		s->flags |= SPRFLAG_INVISIBLE;
	}
	return 0;
}

static int
lgetmessage(lua_State *L) {
	struct sprite *s = self(L);
	lua_pushboolean(L, s->flags & SPRFLAG_MESSAGE);
	return 1;
}

static int
lsetmessage(lua_State *L) {
	struct sprite *s = self(L);
	if(lua_toboolean(L, 2)) {
		s->flags |= SPRFLAG_MESSAGE;
	} else {
		s->flags &= ~SPRFLAG_MESSAGE;
	}
	return 0;
}

static int
lget_force_inherit_frame(lua_State *L) {
	struct sprite *s = self(L);
	lua_pushboolean(L, s->flags & SPRFLAG_FORCE_INHERIT_FRAME);
	return 1;
}

static int
lset_force_inherit_frame(lua_State *L) {
	struct sprite *s = self(L);
	if(lua_toboolean(L, 2)) {
		s->flags |= SPRFLAG_FORCE_INHERIT_FRAME;
	} else {
		s->flags &= ~SPRFLAG_FORCE_INHERIT_FRAME;
	}
	return 0;
}

static int
lsetmat(lua_State *L) {
	struct sprite *s = self(L);
	struct matrix *m = (struct matrix *)lua_touserdata(L, 2);
	if (m == NULL)
		return luaL_error(L, "Need a matrix");
	s->t.mat = &s->mat;
	s->mat = *m;

	return 0;
}

static int
lgetmat(lua_State *L) {
	struct sprite *s = self(L);
	if (s->t.mat == NULL) {
		s->t.mat = &s->mat;
		matrix_identity(&s->mat);
	}
	lua_pushlightuserdata(L, s->t.mat);
	return 1;
}

static int
lgetwmat(lua_State *L) {
	struct sprite *s = self(L);
	if (s->type == TYPE_ANCHOR) {
		lua_pushlightuserdata(L, s->s.mat);
		return 1;
	}
	return luaL_error(L, "Only anchor can get world matrix");
}

static int
lgetwpos(lua_State *L) {
	struct sprite *s = self(L);
	if (s->type == TYPE_ANCHOR) {
		struct matrix* mat = s->s.mat;
		lua_pushnumber(L,mat->m[4] /(float)SCREEN_SCALE);
		lua_pushnumber(L,mat->m[5] /(float)SCREEN_SCALE);
		return 2;
	} else {
		struct srt srt;
		fill_srt(L,&srt,2);

		struct matrix tmp;
		sprite_matrix(s, &tmp);

		int pos[2];
		if (sprite_pos(s, &srt, &tmp, pos) == 0) {
			lua_pushinteger(L, pos[0]);
			lua_pushinteger(L, pos[1]);
			return 2;
		} else {
			return 0;
		}
	}
	return luaL_error(L, "Only anchor can get world matrix");
}

static int
lsetprogram(lua_State *L) {
	struct sprite *s = self(L);
	if (lua_isnoneornil(L,2)) {
		s->t.program = /*PROGRAM_DEFAULT*/0;
	} else {
		s->t.program = (int)luaL_checkinteger(L,2);
	}
	return 0;
}

static int
lsetscissor(lua_State *L) {
	struct sprite *s = self(L);
	if (s->type != TYPE_PANNEL) {
		return luaL_error(L, "Only pannel can set scissor");
	}
	s->data.scissor = lua_toboolean(L,2);
	return 0;
}

static int
lgetname(lua_State *L) {
	struct sprite *s = self(L);
	if (s->name == NULL)
		return 0;
	lua_pushstring(L, s->name);
	return 1;
}

static int
lgettype(lua_State *L) {
	struct sprite *s = self(L);
	lua_pushinteger(L, s->type);
	return 1;
}

static int
lgetparentname(lua_State *L) {
	struct sprite *s = self(L);
	if (s->parent == NULL)
		return 0;
	lua_pushstring(L, s->parent->name);
	return 1;
}

static int
lhasparent(lua_State *L) {
	struct sprite *s = self(L);
	lua_pushboolean(L, s->parent != NULL);
	return 1;
}

static int
lsettext(lua_State *L) {
	struct sprite *s = self(L);    
	if (s->type != TYPE_LABEL) {
		return luaL_error(L, "Only label can set text");
	}

	if (lua_isnoneornil(L, 2)) {
        s->data.text = NULL;
	} else {
		s->data.text = lua_tostring(L, 2);
		lua_createtable(L, 2, 0);
		lua_pushvalue(L, 2);
		lua_rawseti(L, -2, 1);
		lua_pushvalue(L, 3);
		lua_rawseti(L, -2, 2);
		lua_setuservalue(L, 1);		
	}

	return 0;
}

static int
lgettext(lua_State *L) {
	struct sprite *s = self(L);
	if (s->type != TYPE_LABEL) {
		return luaL_error(L, "Only label can get text");
	}
	lua_pushstring(L, s->s.label->text);
	return 1;
}

static int
lgetcolor(lua_State *L) {
	struct sprite *s = self(L);
	if (s->type != TYPE_LABEL)
	{
		lua_pushinteger(L, s->t.color);
	}
	else
	{
		lua_pushinteger(L, s->s.label->font_color);
	}
	return 1;
}

static int
lsetcolor(lua_State *L) {
	struct sprite *s = self(L);
	uint32_t color = luaL_checkinteger(L,2);
	s->t.color = color;
	return 0;
}

static int
lsetalpha(lua_State *L) {
	struct sprite *s = self(L);
	uint8_t alpha = luaL_checkinteger(L, 2);
	s->t.color = (s->t.color & 0x00ffffff) | (alpha << 24);
	return 0;
}

static int
lgetalpha(lua_State *L) {
	struct sprite *s = self(L);
	lua_pushinteger(L, s->t.color >> 24);
	return 1;
}

static int
lgetadditive(lua_State *L) {
	struct sprite *s = self(L);
	lua_pushinteger(L, s->t.additive);
	return 1;
}

static int
lsetadditive(lua_State *L) {
	struct sprite *s = self(L);
	uint32_t additive = luaL_checkinteger(L,2);
	s->t.additive = additive;
	return 0;
}

static int
lgetparent(lua_State *L) {
	struct sprite *s = self(L);
	if (s->parent == NULL)
		return 0;
	lua_getuservalue(L, 1);
	lua_rawgeti(L, -1, 0);
	return 1;
}

static int
lgetprogram(lua_State *L) {
	struct sprite *s = self(L);
	lua_pushinteger(L, s->t.program);
	return 1;
}

/*
static int
lgetreftable(lua_State *L) {
	//struct sprite *s = self(L);
	get_reftable(L, 1);
	return 1;
}*/

//static const char * ud_key = "user_data";
static uint32_t ud_key = 0xFFFF;
static int
lgetusrdata(lua_State *L) {
	get_reftable(L, 1);
	lua_rawgetp(L, -1, &ud_key);
	if (lua_isnoneornil(L, -1)) {
		lua_newtable(L);
		lua_pushvalue(L, -1);  //sprite, uservalue, nil, table, table
		
		lua_rawsetp(L, 2, &ud_key);
	}
	return 1;
}

static void
lgetter(lua_State *L) {
	luaL_Reg l[] = {
		{"frame", lgetframe},
		{"frame_count", lgettotalframe },
		{"visible", lgetvisible },
		{"name", lgetname },
		{"type", lgettype },
		{"text", lgettext},
		{"color", lgetcolor },
		{"alpha", lgetalpha },
		{"additive", lgetadditive },
		{"message", lgetmessage },
		{"force_inherit_frame", lget_force_inherit_frame },
		{"matrix", lgetmat },
		{"world_matrix", lgetwmat },
		{"parent_name", lgetparentname },	// todo: maybe unused , use parent instead
		{"has_parent", lhasparent },	// todo: maybe unused , use parent instead
		{"parent", lgetparent },
		{"program", lgetprogram },
//		{"ref_table", lgetreftable },
		{"usr_data", lgetusrdata },
		{NULL, NULL},
	};
	luaL_newlib(L,l);
}

static void
lsetter(lua_State *L) {
	luaL_Reg l[] = {
		{"frame", lsetframe},
		{"action", lsetaction},
		{"visible", lsetvisible},
		{"matrix" , lsetmat},
		{"text", lsettext},
		{"color", lsetcolor},
		{"alpha", lsetalpha},
		{"additive", lsetadditive },
		{"message", lsetmessage },
		{"force_inherit_frame", lset_force_inherit_frame },
		{"program", lsetprogram },
		{"scissor", lsetscissor },
		{NULL, NULL},
	};
	luaL_newlib(L,l);
}

static void
ref_parent(lua_State *L, int index, int parent) {
	get_reftable(L, index);
	lua_pushvalue(L, parent);
	lua_rawseti(L, -2, 0);	// set self to uservalue[0] (parent)
	lua_pop(L, 1);
}

static void
fetch_parent(lua_State *L, int index) {
	lua_getuservalue(L, 1);
	lua_rawgeti(L, -1, index+1);
	// A child may not exist, but the name is valid. (empty dummy child)
	if (!lua_isnil(L, -1)) {
		ref_parent(L, lua_gettop(L), 1);
	}
}

static int
lfetch(lua_State *L) {
	struct sprite *s = self(L);
	const char * name = luaL_checkstring(L,2);
	int index = sprite_child(s, name);
	if (index < 0)
		return 0;
	if ((s->flags & SPRFLAG_MULTIMOUNT) == 0)	{ // multimount has no parent
		fetch_parent(L, index);
	}

	return 1;
}

static int
lfetch_by_index(lua_State *L) {
	struct sprite *s = self(L);
	if (s->type != TYPE_ANIMATION) {
		return luaL_error(L, "Only animation can fetch by index");
	}
	int index = (int)luaL_checkinteger(L, 2);
	struct pack_animation *ani = s->s.ani;
	if (index < 0 || index >= ani->component_number) {
		return luaL_error(L, "Component index out of range:%d", index);
	}

	fetch_parent(L, index);

	return 1;
}

static void
unlink_parent(lua_State *L, struct sprite * child, int idx) {
	lua_getuservalue(L, idx);	// reftable
	lua_rawgeti(L, -1, 0);	// reftable parent
	struct sprite * parent = (struct sprite *)lua_touserdata(L, -1);
	if (parent == NULL) {
		luaL_error(L, "No parent object");
	}
	int index = sprite_child_ptr(parent, child);
	if (index < 0) {
		luaL_error(L, "Invalid child object");
	}
	lua_getuservalue(L, -1);	// reftable parent parentref
	lua_pushnil(L);
	lua_rawseti(L, -2, index+1);
	lua_pop(L, 3);
	sprite_mount(parent, index, NULL);
}

static int
lmount(lua_State *L) {
	struct sprite *s = self2(L);
	const char * name = luaL_checkstring(L,2);
	int index = sprite_child(s, name);
	if (index < 0) {
		return luaL_error(L, "No child name %s", name);
	}
	lua_getuservalue(L, 1);

	_check_spr_pkg(L, s, index);

	struct sprite * child = (struct sprite *)lua_touserdata(L, 3);

	lua_rawgeti(L, -1, index+1);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
	} else {
		struct sprite * c = (struct sprite *)lua_touserdata(L, -1);
		if (c == child) {
			// mount not change
			return 0;
		}
		if ((c->flags & SPRFLAG_MULTIMOUNT) == 0) {
			// try to remove parent ref
			lua_getuservalue(L, -1);
			if (lua_istable(L, -1)) {
				lua_pushnil(L);
				lua_rawseti(L, -2, 0);
			}
			lua_pop(L, 2);
		} else {
			lua_pop(L, 1);
		}
	}

	if (child == NULL) {
		sprite_mount(s, index, NULL);
		lua_pushnil(L);
		lua_rawseti(L, -2, index+1);
	} else {
		if (child->parent) {
			unlink_parent(L, child, 3);
		}
		sprite_mount(s, index, child);
		lua_pushvalue(L, 3);
		lua_rawseti(L, -2, index+1);

		if ((child->flags & SPRFLAG_MULTIMOUNT) == 0)	{ // multimount has no parent
			// set child's new parent
			ref_parent(L, 3, 1);
		}
	}
	return 0;
}

/*
	userdata sprite
	table { .x .y .sx .sy .rot }
 */
static int
ldraw(lua_State *L) {
	struct sprite *s = self(L);
	struct srt srt;

	fill_srt(L,&srt,2);
	sprite_draw(s, &srt);
	return 0;
}

static int
laabb(lua_State *L) {
	struct sprite *s = self(L);
	struct srt srt;
	fill_srt(L,&srt,2);
	bool world = lua_toboolean(L, 3);
	
	int aabb[4];
	sprite_aabb(s, &srt, world, aabb);
	int i;
	for (i=0;i<4;i++) {
		lua_pushinteger(L, aabb[i]);
	}
	return 4;
}

static int
ltext_size(lua_State *L) {
	struct sprite *s = self(L);
	if (s->type != TYPE_LABEL) {
		return luaL_error(L, "Ony label can get label_size");
	}

	float width, height;
	ej_gtxt_get_size(s, &width, &height);
	lua_pushinteger(L, width);
	lua_pushinteger(L, height);

	lua_pushinteger(L, s->s.label->font_size);
	return 3;
}

static int
lchild_visible(lua_State *L) {
	struct sprite *s = self(L);
	const char * name = luaL_checkstring(L,2);
	lua_pushboolean(L, sprite_child_visible(s, name));
	return 1;
}

static int
lchildren_name(lua_State *L) {
	struct sprite *s = self(L);
	if (s->type != TYPE_ANIMATION)
		return 0;
	int i;
	int cnt=0;
	struct pack_animation * ani = s->s.ani;
	for (i=0;i<ani->component_number;i++) {
		if (ani->component[i].name != NULL) {
			lua_pushstring(L, ani->component[i].name);
			cnt++;
		}
	}
	return cnt;
}

static int
lmatrix_multi_draw(lua_State *L) {
	struct sprite *s = self(L);
	int cnt = (int)luaL_checkinteger(L,3);
	if (cnt == 0)
		return 0;
	luaL_checktype(L,4,LUA_TTABLE);
	luaL_checktype(L,5,LUA_TTABLE);
	if (lua_rawlen(L, 4) < cnt) {
		return luaL_error(L, "matrix length must less then particle count");
	}
	if (lua_rawlen(L, 5) < cnt) {
		return luaL_error(L, "color length must less then particle count");
	}

	struct matrix *mat = (struct matrix *)lua_touserdata(L, 2);

	if (s->t.mat == NULL) {
		s->t.mat = &s->mat;
		matrix_identity(&s->mat);
	}
	struct matrix *parent_mat = s->t.mat;
	uint32_t parent_color = s->t.color;

	int i;
	if (mat) {
		struct matrix tmp;
		for (i = 0; i < cnt; i++) {
			lua_rawgeti(L, 4, i+1);
			lua_rawgeti(L, 5, i+1);
			struct matrix *m = (struct matrix *)lua_touserdata(L, -2);
			matrix_mul(&tmp, m, mat);
			s->t.mat = &tmp;
			s->t.color = (uint32_t)lua_tointeger(L, -1);
			lua_pop(L, 2);

			sprite_draw(s, NULL);
		}
	} else {
		for (i = 0; i < cnt; i++) {
			lua_rawgeti(L, 4, i+1);
			lua_rawgeti(L, 5, i+1);
			struct matrix *m = (struct matrix *)lua_touserdata(L, -2);
			s->t.mat = m;
			s->t.color = (uint32_t)lua_tointeger(L, -1);
			lua_pop(L, 2);

			sprite_draw(s, NULL);
		}
	}

	s->t.mat = parent_mat;
	s->t.color = parent_color;

	return 0;
}

static int
lmulti_draw(lua_State *L) {
	struct sprite *s = self(L);
	int cnt = (int)luaL_checkinteger(L,3);
	if (cnt == 0)
		return 0;
    int n = lua_gettop(L);
	luaL_checktype(L,4,LUA_TTABLE);
	luaL_checktype(L,5,LUA_TTABLE);
	if (lua_rawlen(L, 4) < cnt) {
		return luaL_error(L, "matrix length less then particle count");
	}
    if (n == 6) {
        luaL_checktype(L,6,LUA_TTABLE);
        if (lua_rawlen(L, 6) < cnt) {
            return luaL_error(L, "additive length less then particle count");
        }
    }
	struct srt srt;
	fill_srt(L, &srt, 2);

	if (s->t.mat == NULL) {
		s->t.mat = &s->mat;
		matrix_identity(&s->mat);
	}
	struct matrix *parent_mat = s->t.mat;
	uint32_t parent_color = s->t.color;

	int i;
    if (n == 5) {
        for (i = 0; i < cnt; i++) {
            lua_rawgeti(L, 4, i+1);
            lua_rawgeti(L, 5, i+1);
            s->t.mat = (struct matrix *)lua_touserdata(L, -2);
            s->t.color = (uint32_t)lua_tointeger(L, -1);
            lua_pop(L, 2);

            sprite_draw_as_child(s, &srt, parent_mat, parent_color);
        }
    }else {
        for (i = 0; i < cnt; i++) {
            lua_rawgeti(L, 4, i+1);
            lua_rawgeti(L, 5, i+1);
            lua_rawgeti(L, 6, i+1);
            s->t.mat = (struct matrix *)lua_touserdata(L, -3);
            s->t.color = (uint32_t)lua_tointeger(L, -2);
            s->t.additive = (uint32_t)lua_tointeger(L, -1);
            lua_pop(L, 3);

            sprite_draw_as_child(s, &srt, parent_mat, parent_color);
        }
    }

	s->t.mat = parent_mat;
	s->t.color = parent_color;

	return 0;
}

static struct sprite *
lookup(lua_State *L, struct sprite * spr) {
	int i;
	struct sprite * root = (struct sprite *)lua_touserdata(L, -1);
	lua_getuservalue(L,-1);
	for (i=0;sprite_component(root, i)>=0;i++) {
		struct sprite * child = root->data.children[i];
		if (child) {
			lua_rawgeti(L, -1, i+1);	// parent, reftable, child
			if (child == spr) {
				int child_index = lua_gettop(L);
				int parent_index = child_index - 2;
				ref_parent(L, child_index, parent_index);
				lua_replace(L,-2);	// parent
				return child;
			} else {
				lua_pop(L,1);
			}
		}
	}
	lua_pop(L,1);
	return NULL;
}

static int
unwind(lua_State *L, struct sprite *root, struct sprite *spr) {
	int n = 0;
	while (spr) {
		if (spr == root) {
			return n;
		}
		++n;
		lua_checkstack(L,3);
		lua_pushlightuserdata(L, spr);
		spr = spr->parent;
	}
	return -1;
}

static int
ltest(lua_State *L) {
	struct sprite *s = self(L);
	struct srt srt;
	fill_srt(L,&srt,4);
	float x = luaL_checknumber(L, 2);
	float y = luaL_checknumber(L, 3);
	struct sprite * m = sprite_test(s, &srt, x*SCREEN_SCALE, y*SCREEN_SCALE);
	if (m == NULL)
		return 0;
	if (m==s) {
		lua_settop(L,1);
		return 1;
	}
	lua_settop(L,1);
	int depth = unwind(L, s , m);
	if (depth < 0) {
		return luaL_error(L, "Unwind an invalid sprite");
	}
	int i;
	lua_pushvalue(L,1);
	for (i=depth+1;i>1;i--) {
		struct sprite * tmp = (struct sprite *)lua_touserdata(L, i);
		tmp = lookup(L, tmp);
		if (tmp == NULL) {
			return luaL_error(L, "find an invalid sprite");
		}
		lua_replace(L, -2);
	}

	return 1;
}

static int
lps(lua_State *L) {
	struct sprite *s = self(L);
	struct matrix *m = &s->mat;
	if (s->t.mat == NULL) {
		matrix_identity(m);
		s->t.mat = m;
	}
	int *mat = m->m;
	int n = lua_gettop(L);
	int x,y,scale;
	switch (n) {
	case 4:
		// x,y,scale
		x = luaL_checknumber(L,2) * SCREEN_SCALE;
		y = luaL_checknumber(L,3) * SCREEN_SCALE;
		scale = luaL_checknumber(L,4) * 1024;
		mat[0] = scale;
		mat[1] = 0;
		mat[2] = 0;
		mat[3] = scale;
		mat[4] = x;
		mat[5] = y;
		break;
	case 3:
		// x,y
		x = luaL_checknumber(L,2) * SCREEN_SCALE;
		y = luaL_checknumber(L,3) * SCREEN_SCALE;
		mat[4] = x;
		mat[5] = y;
		break;
	case 2:
		// scale
		scale = luaL_checknumber(L,2) * 1024;
		mat[0] = scale;
		mat[1] = 0;
		mat[2] = 0;
		mat[3] = scale;
		break;
	default:
		return luaL_error(L, "Invalid parm");
	}
	return 0;
}

static int
lsr(lua_State *L) {
	struct sprite *s = self(L);
	struct matrix *m = &s->mat;
	if (s->t.mat == NULL) {
		matrix_identity(m);
		s->t.mat = m;
	}
	int sx=1024,sy=1024,r=0;
	int n = lua_gettop(L);
	switch (n) {
	case 4:
		// sx,sy,rot
		r = luaL_checknumber(L,4) * (EJMAT_R_FACTOR / 360.0);
		// go through
	case 3:
		// sx, sy
		sx = luaL_checknumber(L,2) * 1024;
		sy = luaL_checknumber(L,3) * 1024;
		break;
	case 2:
		// rot
		r = luaL_checknumber(L,2) * (EJMAT_R_FACTOR / 360.0);
		break;
	}
	matrix_sr(m, sx, sy, r);

	return 0;
}

static int
lrecursion_frame(lua_State *L) {
	struct sprite * s = self(L);
	int frame = (int)luaL_checkinteger(L,2);
	int f = sprite_setframe(s, frame, true);
	lua_pushinteger(L, f);
	return 1;
}

static int
lenable_visible_test(lua_State *L) {
	// todo: remove this api
	return 0;
}

static int
lcalc_matrix(lua_State *L) {
	struct sprite * s = self(L);
	struct matrix * mat = (struct matrix *)lua_touserdata(L, 2);
	if (mat == NULL) {
		return luaL_argerror(L, 2, "need a matrix");
	}
	struct matrix *local_matrix = s->t.mat;
	if (local_matrix) {
		struct matrix tmp;
		sprite_matrix(s, &tmp);
		matrix_mul(mat, local_matrix, &tmp);
	} else {
		sprite_matrix(s, mat);
	}

	lua_settop(L, 2);
	return 1;
}

static int
lget_pic_tex_coord(lua_State *L) {
	struct sprite * s = self(L);
	if (s->type != TYPE_PICTURE) {
		return luaL_error(L, "Only picture can get tex coord");
	}
	int index = (int)luaL_checkinteger(L, 2);
	struct pack_picture * pic = s->s.pic;
	if (index < 0 || index >= pic->n) {
		return luaL_error(L, "pic rect index out of range:[0,%d)", pic->n);
	}
	struct pack_quad quad = pic->rect[index];
	int i;
	for (i=0; i<4; i++) {
		lua_pushnumber(L, quad.texture_coord[2*i] / 65535.0f);
		lua_pushnumber(L, quad.texture_coord[2*i+1] / 65535.0f);
	}
	return 8;
}

static void
lmethod(lua_State *L) {
	luaL_Reg l[] = {
		{ "fetch", lfetch },
        { "fetch_by_index", lfetch_by_index },
		{ "mount", lmount },
		{ NULL, NULL },
	};
	luaL_newlib(L,l);

	int i;
	int nk = sizeof(srt_key)/sizeof(srt_key[0]);
	for (i=0;i<nk;i++) {
		lua_pushstring(L, srt_key[i]);
	}
	luaL_Reg l2[] = {
		{ "ps", lps },
		{ "sr", lsr },
		{ "draw", ldraw },
		{ "recursion_frame", lrecursion_frame },
		{ "multi_draw", lmulti_draw },
		{ "matrix_multi_draw", lmatrix_multi_draw },
		{ "test", ltest },
		{ "aabb", laabb },
		{ "text_size", ltext_size},
		{ "child_visible", lchild_visible },
		{ "children_name", lchildren_name },
		{ "world_pos", lgetwpos },
		{ "calc_matrix", lcalc_matrix },
		{ "pic_tex_coord", lget_pic_tex_coord },
		{ NULL, NULL, },
	};
	luaL_setfuncs(L,l2,nk);
}

static int
lnewproxy(lua_State *L) {
	static struct pack_part part = {
		{
			NULL,	// mat
			0xffffffff,	// color
			0,	// additive
			/*PROGRAM_DEFAULT*/0,
			0,	// _dummy
		},	// trans
		0,	// component_id
		0,	// touchable
	};
	static struct pack_frame frame = {
		&part,
		1,	// n
		0,	// _dummy
	};
	static struct pack_action action = {
		NULL,	// name
		1,	// number
		0,	// start_frame
	};
	static struct pack_animation ani = {
		&frame,
		&action,
		1,	// frame_number
		1,	// action_number
		1,	// component_number
		0,	// _dummy
		{{
			"proxy",	// name
			0,	// id
			0,	// _dummy
		}},
	};
	struct sprite * s = (struct sprite *)lua_newuserdata(L, sizeof(struct sprite));
	lua_newtable(L);
	lua_setuservalue(L, -2);

	s->pkg = NULL;
	s->pkg_version = 0;
	s->parent = NULL;
	s->s.ani = &ani;
	s->t.mat = NULL;
	s->t.color = 0xffffffff;
	s->t.additive = 0;
	s->t.rmap = 0xff0000ff;
	s->t.gmap = 0x00ff00ff;
	s->t.bmap = 0x0000ffff;
	s->t.program = /*PROGRAM_DEFAULT*/0;
	s->flags = SPRFLAG_MULTIMOUNT;
	s->time = 0;
	s->name = NULL;
	s->id = 0;
	s->type = TYPE_ANIMATION;
	s->start_frame = 0;
	s->total_frame = 0;
	s->frame = 0;
	s->data.children[0] = NULL;
	sprite_action(s, NULL);

	return 1;
}

static int
lis_particle3d(lua_State* L) {
	struct sprite *s = self(L);
	lua_pushboolean(L, s->type == TYPE_PARTICLE3D || s->type == TYPE_P3D_SPR);
	return 1;
}

static int
lgc(lua_State* L) {
	struct sprite * s = (struct sprite *)lua_touserdata(L, 1);
	if (s == NULL) {
		return 0;
	}
	if (s->type == TYPE_P3D_SPR) {
		ej_ps_sprite_gc(s);
	}
	return 0;
}

int
ejoy2d_sprite(lua_State *L) {
	luaL_Reg l[] ={
		{ "new", lnew },
		{ "proxy", lnewproxy },
		{ "enable_visible_test", lenable_visible_test },
		{ "is_particle3d", lis_particle3d },
		{ "gc", lgc },
		{ NULL, NULL },
	};
	luaL_newlib(L,l);

	lmethod(L);
	lua_setfield(L, -2, "method");
	lgetter(L);
	lua_setfield(L, -2, "get");
	lsetter(L);
	lua_setfield(L, -2, "set");

	return 1;
}
