#ifndef ej_gtxt_h
#define ej_gtxt_h

struct srt;
struct sprite_trans;
struct sprite;

void ej_gtxt_init();

void ej_gtxt_draw(struct sprite*, const struct srt*, const struct sprite_trans*);

void ej_gtxt_get_size(struct sprite*, float* width, float* height);

struct sprite* ej_gtxt_point_query(struct sprite*, int x, int y, const struct srt*);

#endif // ej_gtxt_h