// SPDX-License-Identifier: Apache-2.0
#include "common.h"
#include "model.h"
#include "physics-common.h"

phys_capsule phys_capsule_size(entity3d *e, double radius, double offset)
{
    float X = entity3d_aabb_X(e), Y = entity3d_aabb_Y(e), Z = entity3d_aabb_Z(e);
    phys_capsule cap = { .axis = xmax3(X, Y, Z) };

    switch (cap.axis) {
        case 0:
        case 1:
            /*
             * Upright
             */
            cap.radius  = radius ? radius : min3(X, Y, Z) / 2;
            cap.length  = max(Y / 2 - cap.radius * 2, 0);
            cap.yoffset = offset ? offset : Y / 2;
            cap.ray_off = cap.radius + cap.length / 2;
            break;
        case 2:
            /*
             * Puppy
             */
            cap.radius  = radius ? radius : X / 2;
            cap.length  = Z - cap.radius * 2;
            cap.yoffset = offset ? offset : (Y - cap.radius * 2) / 2;
            cap.ray_off = cap.radius;
            break;
    }

    /*
     * XXX: The above logic is busted: ray length ends up zero.
     */

    return cap;
}
