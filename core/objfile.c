// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include "util.h"
#include "objfile.h"
#ifdef OBJ2BIN
#define dbg(fmt, args...) printf(fmt, ## args)
#else
#include "logger.h"
#endif /* OBJ2BIN */

/****************************************************************************
 * Model data
 * Vertices, faces, normal vectors, etc
 ****************************************************************************/
int model_data_init(struct model_data *md,
                    unsigned long nr_v,
                    unsigned long nr_vt,
                    unsigned long nr_vn,
                    unsigned long nr_f)
{
    if (!nr_v)
        return -EINVAL;

    memset(md, 0, sizeof(*md));

    md->v = malloc(sizeof(float) * nr_v * 3);
    if (!md->v)
        return -ENOMEM;

    if (nr_vt) {
        md->vt = malloc(sizeof(float) * nr_vt * 2);
        if (!md->vt)
            goto err_vt;
    }

    if (nr_vn) {
        md->vn = malloc(sizeof(float) * nr_vn * 3);
        if (!md->vn)
            goto err_vn;
    }

    if (nr_f) {
        md->f = malloc(sizeof(int) * nr_f * 9);
        if (!md->f)
            goto err_f;
    }

    md->nr_v  = nr_v  * 3;
    md->nr_vt = nr_vt * 2;
    md->nr_vn = nr_vn * 3;
    md->nr_f  = nr_f  * 9;
    dbg("nr_v %lu/%lu nr_vt %lu/%lu nr_vn %lu/%lu nr_f %lu/%lu\n",
        nr_v, md->nr_v, nr_vt, md->nr_vt, nr_vn, md->nr_vn, nr_f, md->nr_f);

    return 0;
err_f:
    free(md->vn);
err_vn:
    free(md->vt);
err_vt:
    free(md->v);
    return -ENOMEM;
}

int model_data_push_v(struct model_data *md, float v0, float v1, float v2)
{
    if (md->loaded_v >= md->nr_v)
        return -ENOSPC;

    md->v[md->loaded_v++] = v0;
    md->v[md->loaded_v++] = v1;
    md->v[md->loaded_v++] = v2;

    return 0;
}

int model_data_push_vt(struct model_data *md, float v0, float v1)
{
    if (md->loaded_vt >= md->nr_vt)
        return -ENOSPC;

    md->vt[md->loaded_vt++] = v0;
    md->vt[md->loaded_vt++] = v1;

    return 0;
}

int model_data_push_vn(struct model_data *md, float v0, float v1, float v2)
{
    if (md->loaded_vn >= md->nr_vn)
        return -ENOSPC;

    md->vn[md->loaded_vn++] = v0;
    md->vn[md->loaded_vn++] = v1;
    md->vn[md->loaded_vn++] = v2;

    return 0;
}

int model_data_push_f(struct model_data *md, int f[9])
{
    int i;

    if (md->loaded_f >= md->nr_f)
        return -ENOSPC;

    for (i = 0; i < 9; i++)
        md->f[md->loaded_f++] = f[i] - 1;

    return 0;
}

struct model_data *model_data_new_from_obj(const char *base, size_t size)
{
    const char *p, *pend;
    int s, vecs = 0, other = 0, vts = 0, vns = 0, fs = 0, pass = 0;
    struct model_data *md;

    if (!size)
        return NULL;

    md = malloc(sizeof(*md));
    if (!md)
        return NULL;

again:
    for (s = 0, p = base; p - base < size;) {
        float r0, r1, r2;
        int f[9];

        pend = strchr(p, '\n');
        if (!pend)
            pend = p + strlen(p);

        switch (*p++) {
        case '#':
            //sscanf(p, "%m[^\n]", &str);
            //trace("comment '%s'\n", str);
            //free(str);
            break;
        case 'o':
            p = skip_space(p);
            /* not sure what to do with this yet */
            p = skip_nonspace(p);
            break;
        case 's':
            p = skip_space(p);
            sscanf(p, "%d", &s);
            break;
        case 'f':
            if (!pass) {
                fs++;
            } else {
                p = skip_space(p);
                /* XXX: not good enough, there may be gaps, see f-16.obj */
                sscanf(p, "%d/%d/%d %d/%d/%d %d/%d/%d",
                       &f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &f[6], &f[7], &f[8]);
                model_data_push_f(md, f);
            }
            break;

        case 'v': {
            switch (*p++) {
                case ' ':
                    if (!pass) {
                        vecs++;
                        break;
                    }
                    p = skip_space(p);
                    sscanf(p, "%f %f %f", &r0, &r1, &r2);
                    model_data_push_v(md, r0, r1, r2);
                    //if (!vecs)
                        //trace(" => %f %f %f\n", r0, r1, r2);
                    break;
                case 'n':
                    if (!pass) {
                        vns++;
                        break;
                    }
                    p = skip_space(p);
                    sscanf(p, "%f %f %f", &r0, &r1, &r2);
                    model_data_push_vn(md, r0, r1, r2);
                    //if (!vns)
                        //trace(" => %f %f %f\n", r0, r1, r2);
                    break;
                case 't':
                    if (!pass) {
                        vts++;
                        break;
                    }
                    p = skip_space(p);
                    s = sscanf(p, "%f %f", &r0, &r1);
                    model_data_push_vt(md, r0, r1);
                    //if (!vts)
                        //trace(" => %f %f, %d\n", r0, r1, s);
                    break;
            }
            break;
        }
        default:
            //sscanf(p, "%m[^ ]", &str);
            other++;
            break;
        }
        p = pend + 1;
    }

    if (!pass) {
        model_data_init(md, vecs, vts, vns, fs);
        pass++;
        goto again;
    }
    dbg("got vecs: %d vts: %d vns: %d fs: %d other: %d\n", vecs, vts, vns, fs, other);

    return md;
}

void model_data_free(struct model_data *md)
{
    free(md->v);
    free(md->vn);
    free(md->vt);
    free(md->f);
    free(md);
}

int model_data_to_vectors(struct model_data *md,
                          float **txp, size_t *txszp,
                          float **normp, size_t *vxszp,
                          unsigned short **idxp, size_t *idxszp)
{
    unsigned long i = 0, f, fx;
    unsigned long fidx;
    unsigned long vidx;
    unsigned long vtidx;
    unsigned long vnidx;
    unsigned short *idx;
    float *tx, *norm;

    *vxszp  = sizeof(*norm) * md->nr_v;
    *txszp  = md->nr_vt ? sizeof(*tx) * md->nr_v : 0;
    *idxszp = sizeof(*idx)  * md->nr_f / 3;
    norm = malloc(*vxszp);
    tx   = *txszp ? malloc(*txszp) : NULL;
    idx  = malloc(*idxszp);

    /*
     * face is v0[f]/t0[f]/n0[f] v1[f]/t1[f]/n1[f] v2[f]/t2[f]/n2[f], so
     * vertex[v0[f]] <- md_v[v0[f]]: unchanged
     * normal[v0[f]] <- md_vn[n0[f]] (3f)
     * tex[v0[f]] <- md_vt[t0[f]]    (2f)
     * index <- [v0, v1, v2]
     */
    for (f = 0; f < md->nr_f; f += 9) {
        for (fx = 0; fx < 3; fx++) {
            fidx  = f + fx*3;
            //dbg("nr_v %lu nr_f %lu fidx %lu\n", md->nr_v, md->nr_f, fidx);
            vidx  = md->f[fidx];
            vtidx = md->f[fidx+1];
            vnidx = md->f[fidx+2];

            if (tx) {
                tx[vidx * 2]     = md->vt[vtidx * 2];
                tx[vidx * 2 + 1] = 1 - md->vt[vtidx * 2 + 1];
            }

            norm[vidx * 3]     = md->vn[vnidx * 3];
            norm[vidx * 3 + 1] = md->vn[vnidx * 3 + 1];
            norm[vidx * 3 + 2] = md->vn[vnidx * 3 + 2];

            idx[i++] = vidx;
        }
    }
    //fprintf(stderr, "fidx %lu vidx %lu vtidx %lu vnidx %lu\n", fidx, vidx, vtidx, vnidx);
    *txp   = tx;
    *normp = norm;
    *idxp = idx;

    return 0;
}

#ifdef OBJ2BIN
int main(int argc, char **argv)
{
    struct bin_vec_header H = {
        .magic  = 0x12345678,
        .ver    = 1,
    };
    struct model_data *md;
    float *tx, *norm;
    unsigned short *idx;
    size_t vxsz, txsz, idxsz;
    FILE *in, *out;
    struct stat st;
    void *inbuf;

    if (argc != 3) {
        fprintf(stderr, "wrong number of parameters: %d\n", argc);
        return EXIT_FAILURE;
    }

    fprintf(stderr, "in: %s\n", argv[1]);
    fprintf(stderr, "out: %s\n", argv[2]);

    in = fopen(argv[1], "r");
    if (!in)
        return EXIT_FAILURE;

    out = fopen(argv[2], "w");
    if (!out)
        return EXIT_FAILURE;

    fstat(fileno(in), &st);
    inbuf = malloc(st.st_size);
    fread(inbuf, st.st_size, 1, in);
    fclose(in);
    md = model_data_new_from_obj(inbuf, st.st_size);
    fprintf(stderr, "nr_v %d/%d nr_vn %d/%d nr_vt %d/%d nr_f %d/%d\n",
            md->nr_v, md->loaded_v,
            md->nr_vn, md->loaded_vn,
            md->nr_vt, md->loaded_vt,
            md->nr_f, md->loaded_f
           );
    model_data_to_vectors(md, &tx, &txsz, &norm, &vxsz, &idx, &idxsz);
    fprintf(stderr, "vxsz: %zu txsz: %zu idxsz: %d\n", vxsz, txsz, idxsz);
    H.nr_vertices = idxsz / sizeof(*idx);
    H.vxsz = vxsz;
    H.txsz = txsz;
    H.idxsz = idxsz;
    fwrite(&H, sizeof(H), 1, out);
    fwrite(md->v,   vxsz, 1, out);
    fwrite(tx,      txsz, 1, out);
    fwrite(norm,    vxsz, 1, out);
    fwrite(idx,    idxsz, 1, out);
    fclose(out);
    return EXIT_SUCCESS;
}
#endif /* OBJ2BIN */
