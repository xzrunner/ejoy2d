#ifndef ej_rvg_h
#define ej_rvg_h

struct pack_shape;
struct srt;
struct sprite_trans;

void ej_rvg_draw(struct pack_shape*, const struct srt*, const struct sprite_trans*);

#endif // ej_rvg_h