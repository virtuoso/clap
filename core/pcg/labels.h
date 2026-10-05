/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_LABELS_H__
#define __CLAP_PCG_LABELS_H__

/* Internal to the scene commit: pcg/commit.c letters the labels with this. */

#include "pcg/commit.h"

/**
 * pcg_labels_build() - letter a sink's labels into meshes over atlases
 * @s:      the sink
 * @opts:   the commit's options: the library and the font
 * @fn:     called with every mesh made, keyed by the material lettered
 *          for it, the chunk and the collide class; the mesh is dropped
 *          when @fn returns
 * @priv:   passed to @fn
 *
 * Groups the labels by chunk, material and collide class. Each group
 * gets an atlas canvas filled with its material's plate colour, every
 * label printed into a cell of its own in the ink colour, uploaded
 * through the library and worn by a material derived from the group's,
 * and a mesh of one quad per label over its cell.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for labels without a font; the
 * first error @fn returned; canvas, library and mesh errors
 */
cerr_check pcg_labels_build(pcg_sink *s, const pcg_commit_opts *opts, pcg_sink_mesh_fn fn, void *priv);

#endif /* __CLAP_PCG_LABELS_H__ */
