#include <errno.h>
#include "model.h"
#include "scene.h"
#include "shader.h"

#define SIZE 1000
int terrain_init(struct scene *s, float vpos, unsigned int nr_v)
{
    struct model3d *model;
    struct shader_prog *prog = shader_prog_find(s->prog, "model"); /* XXX */
    unsigned long total = nr_v * nr_v, it;
    size_t vxsz, txsz, idxsz;
    float *vx, *norm, *tx;
    unsigned short *idx;
    int i, j;

    vxsz  = total * sizeof(*vx) * 3;
    txsz  = total * sizeof(*tx) * 2;
    idxsz = 6 * (nr_v - 1) * (nr_v - 1) * sizeof(*idx);
    vx    = malloc(vxsz);
    norm  = malloc(vxsz);
    tx    = malloc(txsz);
    idx   = malloc(idxsz);
    if (!vx || !norm || !tx || !idx)
        return -ENOMEM;

    for (it = 0, i = 0; i < nr_v; i++)
        for (j = 0; j < nr_v; j++) {
            vx[it * 3 + 0] = (float)j / ((float)nr_v - 1) * SIZE - SIZE/2;
            vx[it * 3 + 1] = vpos + sin(to_radians((float)rand()));
            vx[it * 3 + 2] = (float)i / ((float)nr_v - 1) * SIZE - SIZE/2;
            norm[it * 3 + 0] = 0;
            norm[it * 3 + 1] = 1;
            norm[it * 3 + 2] = 0;
            tx[it * 2 + 0] = (float)j*32 / ((float)nr_v - 1);
            tx[it * 2 + 1] = (float)i*32 / ((float)nr_v - 1);
            it++;
        }
    //dbg("it %lu nr_v %lu total %lu vxsz %lu txsz %lu idxsz %lu\n", it, nr_v, total,
    //    vxsz, txsz, idxsz);

    for (it = 0, i = 0; i < nr_v - 1; i++)
        for (j = 0; j < nr_v - 1; j++) {
            int top_left = i * nr_v + j;
            int top_right = top_left + 1;
            int bottom_left = (i + 1) * nr_v + j;
            int bottom_right = bottom_left + 1;
            idx[it++] = top_left;
            idx[it++] = bottom_left;
            idx[it++] = top_right;
            idx[it++] = top_right;
            idx[it++] = bottom_left;
            idx[it++] = bottom_right;
        }
    //dbg("it %lu\n", it);
    model = model3d_new_from_vectors("terrain", prog, vx, vxsz, idx, idxsz,
                                     tx, txsz, norm, vxsz);
    free(vx);
    free(tx);
    free(norm);
    free(idx);

    model3d_add_texture(model, "grass20.png");
    scene_add_model(s, model);
    create_entities(model);
    return 0;
}
