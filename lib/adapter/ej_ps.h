#ifndef ej_ps_h
#define ej_ps_h

#include <lua.h>
#include <lauxlib.h>

struct sprite;
struct srt;
struct sprite_trans;

void ej_ps_set_lua_state(lua_State* L);
int ej_sprite_sprite_ref(int idx);

void ej_ps_init();

void ej_ps_pause();
void ej_ps_resume();

void ej_ps_update(float dt);
void ej_ps_draw(struct sprite*, const struct srt*, const struct sprite_trans*);
void ej_ps_mount(struct sprite* parent, struct sprite* old_child, struct sprite* new_child);

void ej_ps_sprite_gc(struct sprite*);

#endif // ej_ps_h