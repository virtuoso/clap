/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_COMPAT_SHARED_BITS_H__
#define __CLAP_COMPAT_SHARED_BITS_H__

#ifndef HAVE_FFS
static inline int ffs(int x)
{
    return __builtin_ffs(x);
}
#endif /* HAVE_FFS */

#ifndef HAVE_FFSL
static inline int ffsl(long x)
{
    return __builtin_ffsl(x);
}
#endif /* HAVE_FFSL */

/* matching ffs*(), count bit positions from 1; 0 means no bits are set */
#ifndef HAVE_FLS
static inline int fls(int x)
{
    return x ? sizeof(x) * 8 - __builtin_clz(x) : 0;
}
#endif /* HAVE_FLS */

#endif /* __CLAP_COMPAT_SHARED_BITS_H__ */
