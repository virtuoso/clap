#ifndef __CLAP_TERRAIN_H__
#define __CLAP_TERRAIN_H__

struct terrain {
    struct model3d  *model;
};

int terrain_init(struct scene *s, float vpos, unsigned int nr_v);

#endif /* __CLAP_TERRAIN_H__ */