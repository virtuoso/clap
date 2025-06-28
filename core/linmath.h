/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LINMATH_H
#define LINMATH_H

#include <math.h>
#include <string.h>

#if defined(__ARM_NEON) || defined(__EMSCRIPTEN__)
#include <arm_neon.h>
#define USE_SIMD 1
#endif /* __ARM_NEON || __EMSCRIPTEN__ */

#ifdef LINMATH_NO_INLINE
#define LINMATH_H_FUNC static
#else
#define LINMATH_H_FUNC static inline
#endif

#define LINMATH_H_DEFINE_VEC(n) \
typedef float vec##n[n]; \
LINMATH_H_FUNC void vec##n##_add(vec##n r, vec##n const a, vec##n const b) \
{ \
	int i; \
	for(i=0; i<n; ++i) \
		r[i] = a[i] + b[i]; \
} \
LINMATH_H_FUNC void vec##n##_sub(vec##n r, vec##n const a, vec##n const b) \
{ \
	int i; \
	for(i=0; i<n; ++i) \
		r[i] = a[i] - b[i]; \
} \
LINMATH_H_FUNC void vec##n##_scale(vec##n r, vec##n const v, float const s) \
{ \
	int i; \
	for(i=0; i<n; ++i) \
		r[i] = v[i] * s; \
} \
LINMATH_H_FUNC float vec##n##_mul_inner(vec##n const a, vec##n const b) \
{ \
	float p = 0.; \
	int i; \
	for(i=0; i<n; ++i) \
		p += b[i]*a[i]; \
	return p; \
} \
LINMATH_H_FUNC float vec##n##_len(vec##n const v) \
{ \
	return sqrtf(vec##n##_mul_inner(v,v)); \
} \
LINMATH_H_FUNC void vec##n##_dup(vec##n r, vec##n const v) \
{ \
	int i; \
	for(i=0; i<n; ++i) \
		r[i] = v[i]; \
} \
LINMATH_H_FUNC void vec##n##_norm(vec##n r, vec##n const v) \
{ \
	float k = 1.0 / vec##n##_len(v); \
	vec##n##_scale(r, v, k); \
} \
LINMATH_H_FUNC void vec##n##_norm_safe(vec##n r, vec##n const v) \
{ \
	if (vec##n##_len(v)) \
		vec##n##_norm(r, v); \
	else \
		vec##n##_dup(r, v); \
} \
LINMATH_H_FUNC void vec##n##_min(vec##n r, vec##n const a, vec##n const b) \
{ \
	int i; \
	for(i=0; i<n; ++i) \
		r[i] = a[i]<b[i] ? a[i] : b[i]; \
} \
LINMATH_H_FUNC void vec##n##_max(vec##n r, vec##n const a, vec##n const b) \
{ \
	int i; \
	for(i=0; i<n; ++i) \
		r[i] = a[i]>b[i] ? a[i] : b[i]; \
} \
LINMATH_H_FUNC void vec##n##_add_scaled(vec##n r, vec##n const a, vec##n const b, float const sa, float const sb) \
{ \
	int i; \
	for(i=0; i<n; ++i) \
		r[i] = a[i] * sa + b[i] * sb; \
} \
LINMATH_H_FUNC void vec##n##_pow(vec##n r, vec##n const a, const float exp) \
{ \
	int i; \
	for(i=0; i<n; ++i) \
		r[i] = powf(a[i], exp); \
} \
LINMATH_H_FUNC void vec##n##_pow_vec##n(vec##n r, vec##n const a, vec##n const exp) \
{ \
	int i; \
	for(i=0; i<n; ++i) \
		r[i] = powf(a[i], exp[i]); \
}

LINMATH_H_DEFINE_VEC(2)
LINMATH_H_DEFINE_VEC(3)
#ifdef USE_SIMD
typedef float vec4[4];
LINMATH_H_FUNC void vec4_add(vec4 r, vec4 const a, vec4 const b)
{
    float32x4_t a0, b0, r0;

    a0 = vld1q_f32(a);
    b0 = vld1q_f32(b);

    r0 = vaddq_f32(a0, b0);

    vst1q_f32(r, r0);
}

LINMATH_H_FUNC void vec4_sub(vec4 r, vec4 const a, vec4 const b)
{
    float32x4_t a0, b0, r0;

    a0 = vld1q_f32(a);
    b0 = vld1q_f32(b);

    r0 = vsubq_f32(a0, b0);

    vst1q_f32(r, r0);
}

LINMATH_H_FUNC void vec4_scale(vec4 r, vec4 const v, float const s)
{
    float32x4_t v0, r0;

    v0 = vld1q_f32(v);

    r0 = vmulq_n_f32(v0, s);

    vst1q_f32(r, r0);
}

LINMATH_H_FUNC float vec4_mul_inner(vec4 const a, vec4 const b)
{
    float32x4_t a0, b0, r0;

    a0 = vld1q_f32(a);
    b0 = vld1q_f32(b);

    r0 = vmulq_f32(a0, b0);

    return vaddvq_f32(r0);
}

LINMATH_H_FUNC float vec4_len(vec4 const v)
{
    return sqrtf(vec4_mul_inner(v,v));
}

LINMATH_H_FUNC void vec4_dup(vec4 r, vec4 const v)
{
    float32x4_t v0;

    v0 = vld1q_f32(v);
    vst1q_f32(r, v0);
}

LINMATH_H_FUNC void vec4_norm(vec4 r, vec4 const v)
{
    float k = 1.0 / vec4_len(v);
    vec4_scale(r, v, k);
}

LINMATH_H_FUNC void vec4_norm_safe(vec4 r, vec4 const v)
{
    if (vec4_len(v))
        vec4_norm(r, v);
    else
        vec4_dup(r, v);
}

LINMATH_H_FUNC void vec4_min(vec4 r, vec4 const a, vec4 const b)
{
    float32x4_t a0, b0, r0;

    a0 = vld1q_f32(a);
    b0 = vld1q_f32(b);

    r0 = vminq_f32(a0, b0);

    vst1q_f32(r, r0);
}

LINMATH_H_FUNC void vec4_max(vec4 r, vec4 const a, vec4 const b)
{
    float32x4_t a0, b0, r0;

    a0 = vld1q_f32(a);
    b0 = vld1q_f32(b);

    r0 = vmaxq_f32(a0, b0);

    vst1q_f32(r, r0);
}

LINMATH_H_FUNC void vec4_add_scaled(vec4 r, vec4 const a, vec4 const b, float const sa, float const sb)
{
    float32x4_t a0, b0, r0;

    a0 = vld1q_f32(a);
    b0 = vld1q_f32(b);
    r0 = vdupq_n_f32(0);

    r0 = vfmaq_n_f32(r0, a0, sa);
    r0 = vfmaq_n_f32(r0, b0, sb);

    vst1q_f32(r, r0);
}

LINMATH_H_FUNC void vec4_pow(vec4 r, vec4 const a, const float exp)
{
    int i;
    for (i = 0; i < 4; ++i)
        r[i] = powf(a[i], exp);
}

LINMATH_H_FUNC void vec4_pow_vec4(vec4 r, vec4 const a, vec4 const exp)
{
    int i;
    for (i = 0; i < 4; ++i)
        r[i] = powf(a[i], exp[i]);
}
#else /* USE_SIMD */
LINMATH_H_DEFINE_VEC(4)
#endif /* USE_SIMD */


LINMATH_H_FUNC void vec3_setup(vec3 v, float x, float y, float z)
{
	v[0] = x;
	v[1] = y;
	v[2] = z;
}

LINMATH_H_FUNC void vec4_setup(vec4 v, float x, float y, float z, float w)
{
	v[0] = x;
	v[1] = y;
	v[2] = z;
	v[3] = w;
}

LINMATH_H_FUNC void vec3_mul_cross(vec3 r, vec3 const a, vec3 const b)
{
	r[0] = a[1]*b[2] - a[2]*b[1];
	r[1] = a[2]*b[0] - a[0]*b[2];
	r[2] = a[0]*b[1] - a[1]*b[0];
}

LINMATH_H_FUNC void vec3_reflect(vec3 r, vec3 const v, vec3 const n)
{
	float p  = 2.f*vec3_mul_inner(v, n);
	int i;
	for(i=0;i<3;++i)
		r[i] = v[i] - p*n[i];
}

LINMATH_H_FUNC void vec4_mul_cross(vec4 r, vec4 a, vec4 b)
{
	r[0] = a[1]*b[2] - a[2]*b[1];
	r[1] = a[2]*b[0] - a[0]*b[2];
	r[2] = a[0]*b[1] - a[1]*b[0];
	r[3] = 1.f;
}

LINMATH_H_FUNC void vec4_reflect(vec4 r, vec4 v, vec4 n)
{
	float p  = 2.f*vec4_mul_inner(v, n);
	int i;
	for(i=0;i<4;++i)
		r[i] = v[i] - p*n[i];
}

#define LINMATH_DEFINE_MAT(n) \
typedef vec## n mat## n ## x ## n[n]; \
LINMATH_H_FUNC void mat## n ## x ## n ##_identity(mat## n ## x ## n  M) \
{ \
	int i, j; \
	for(i=0; i<n; ++i) \
		for(j=0; j<n; ++j) \
			M[i][j] = i==j ? 1.f : 0.f; \
} \
LINMATH_H_FUNC void mat## n ## x ## n ##_dup(mat## n ## x ## n  M, mat## n ## x ## n  N) \
{ \
	int i, j; \
	for(i=0; i<n; ++i) \
		for(j=0; j<n; ++j) \
			M[i][j] = N[i][j]; \
} \
LINMATH_H_FUNC void mat## n ## x ## n ##_mul_vec## n ##_post(vec## n r, mat## n ## x ## n M, vec## n v) \
{ \
	vec4 temp; \
    for (int i = 0; i < n; ++i) { \
        temp[i] = M[0][i]*v[0] + M[1][i]*v[1] + M[2][i]*v[2]; \
		if (n == 4) temp[i] += M[3][i]*v[3]; \
	} \
	vec## n ##_dup(r, temp); \
}

#define LINMATH_DEFINE_MAT_MUL(n) \
LINMATH_H_FUNC void mat## n ## x ## n ##_mul_vec## n(vec## n r, mat## n ## x ## n M, vec## n v) \
{ \
	int i, j; \
	for(j=0; j<n; ++j) { \
		r[j] = 0.f; \
		for(i=0; i<n; ++i) \
			r[j] += M[i][j] * v[i]; \
	} \
} \
LINMATH_H_FUNC void mat## n ## x ## n ##_transpose(mat## n ## x ## n M, mat## n ## x ## n N) \
{ \
	int i, j; \
	mat## n ##x## n temp = {}; \
	for(j=0; j<n; ++j) \
		for(i=0; i<n; ++i) \
			temp[i][j] = N[j][i]; \
	mat## n ##x## n ##_dup(M, temp); \
}

LINMATH_DEFINE_MAT(2);
LINMATH_DEFINE_MAT(3);
LINMATH_DEFINE_MAT(4);
LINMATH_DEFINE_MAT_MUL(2);
LINMATH_DEFINE_MAT_MUL(3);

#ifdef USE_SIMD
LINMATH_H_FUNC void mat4x4_mul_vec4(vec4 r, mat4x4 M, vec4 v) {
    float32x4_t v0;
    float32x4_t w;
    float32x4_t m0, m1, m2, m3;

    w = vmovq_n_f32(0);

    v0 = vld1q_f32(v);

    m0 = vld1q_f32(M[0]);
    m1 = vld1q_f32(M[1]);
    m2 = vld1q_f32(M[2]);
    m3 = vld1q_f32(M[3]);

    w = vfmaq_laneq_f32(w, m0, v0, 0);
    w = vfmaq_laneq_f32(w, m1, v0, 1);
    w = vfmaq_laneq_f32(w, m2, v0, 2);
    w = vfmaq_laneq_f32(w, m3, v0, 3);

    vst1q_f32(r, w);
}
LINMATH_H_FUNC void mat4x4_transpose(mat4x4 M, mat4x4 N)
{
    float32x4_t m0, m1, m2, m3;
    float32x4_t n0, n1, n2, n3;
    float32x4_t a, b, c, d;

    n0 = vld1q_f32(N[0]);
    n1 = vld1q_f32(N[1]);
    n2 = vld1q_f32(N[2]);
    n3 = vld1q_f32(N[3]);

    a = vtrn1q_f32(n0, n1);
    b = vtrn2q_f32(n0, n1);
    c = vtrn1q_f32(n2, n3);
    d = vtrn2q_f32(n2, n3);

    m0 = vtrn1q_f64(a, c);
    m1 = vtrn1q_f64(b, d);
    m2 = vtrn2q_f64(a, c);
    m3 = vtrn2q_f64(b, d);

    vst1q_f32(M[0], m0);
    vst1q_f32(M[1], m1);
    vst1q_f32(M[2], m2);
    vst1q_f32(M[3], m3);
}
#else /* USE_SIMD */
LINMATH_DEFINE_MAT_MUL(4);
#endif /* USE_SIMD */

#ifdef USE_SIMD
LINMATH_H_FUNC void mat4x4_transpose_mat3x3(mat4x4 m)
{
    uint8x16x3_t a;
    uint8x16_t idx0, idx1, idx2;
    uint8x16_t r0, r1, r2;

    a.val[0] = vreinterpretq_u8_f32(vld1q_f32(m[0]));
    a.val[1] = vreinterpretq_u8_f32(vld1q_f32(m[1]));
    a.val[2] = vreinterpretq_u8_f32(vld1q_f32(m[2]));
    idx0 = vcombine_u8(vcreate_u8(0x1312111003020100), vcreate_u8(0x0f0e0d0c23222120));
    idx1 = vcombine_u8(vcreate_u8(0x1716151407060504), vcreate_u8(0x1f1e1d1c27262524));
    idx2 = vcombine_u8(vcreate_u8(0x1b1a19180b0a0908), vcreate_u8(0x2f2e2d2c2b2a2928));

    r0 = vqtbl3q_u8(a, idx0);
    r1 = vqtbl3q_u8(a, idx1);
    r2 = vqtbl3q_u8(a, idx2);

    vst1q_f32(m[0], vreinterpretq_f32_u8(r0));
    vst1q_f32(m[1], vreinterpretq_f32_u8(r1));
    vst1q_f32(m[2], vreinterpretq_f32_u8(r2));
}
#else /* USE_SIMD */
LINMATH_H_FUNC void mat4x4_transpose_mat3x3(mat4x4 m)
{
    mat4x4 r;
    mat4x4_dup(r, m);
    vec3_dup(r[0], (vec3){ m[0][0], m[1][0], m[2][0] });
    vec3_dup(r[1], (vec3){ m[0][1], m[1][1], m[2][1] });
    vec3_dup(r[2], (vec3){ m[0][2], m[1][2], m[2][2] });
    mat4x4_dup(m, r);
}
#endif /* USE_SIMD */
LINMATH_H_FUNC void mat4x4_row(vec4 r, mat4x4 M, int i)
{
	int k;
	for(k=0; k<4; ++k)
		r[k] = M[k][i];
}
LINMATH_H_FUNC void mat4x4_col(vec4 r, mat4x4 M, int i)
{
	int k;
	for(k=0; k<4; ++k)
		r[k] = M[i][k];
}
LINMATH_H_FUNC void mat4x4_add(mat4x4 M, mat4x4 a, mat4x4 b)
{
	int i;
	for(i=0; i<4; ++i)
		vec4_add(M[i], a[i], b[i]);
}
LINMATH_H_FUNC void mat4x4_sub(mat4x4 M, mat4x4 a, mat4x4 b)
{
	int i;
	for(i=0; i<4; ++i)
		vec4_sub(M[i], a[i], b[i]);
}
LINMATH_H_FUNC void mat4x4_scale(mat4x4 M, mat4x4 a, float k)
{
	int i;
	for(i=0; i<4; ++i)
		vec4_scale(M[i], a[i], k);
}
LINMATH_H_FUNC void mat4x4_scale_aniso(mat4x4 M, mat4x4 a, float x, float y, float z)
{
	int i;
	vec4_scale(M[0], a[0], x);
	vec4_scale(M[1], a[1], y);
	vec4_scale(M[2], a[2], z);
	for(i = 0; i < 4; ++i) {
		M[3][i] = a[3][i];
	}
}
#ifdef USE_SIMD
LINMATH_H_FUNC void mat4x4_mul(mat4x4 M, mat4x4 a, mat4x4 b)
{
    float32x4_t a0, a1, a2, a3;
    float32x4_t b0, b1, b2, b3;
    float32x4_t c0, c1, c2, c3;

    c0 = vmovq_n_f32(0);
    c1 = vmovq_n_f32(0);
    c2 = vmovq_n_f32(0);
    c3 = vmovq_n_f32(0);

    a0 = vld1q_f32(a[0]);
    a1 = vld1q_f32(a[1]);
    a2 = vld1q_f32(a[2]);
    a3 = vld1q_f32(a[3]);

    b0 = vld1q_f32(b[0]);
    b1 = vld1q_f32(b[1]);
    b2 = vld1q_f32(b[2]);
    b3 = vld1q_f32(b[3]);

    c0 = vfmaq_laneq_f32(c0, a0, b0, 0);
    c0 = vfmaq_laneq_f32(c0, a1, b0, 1);
    c0 = vfmaq_laneq_f32(c0, a2, b0, 2);
    c0 = vfmaq_laneq_f32(c0, a3, b0, 3);

    c1 = vfmaq_laneq_f32(c1, a0, b1, 0);
    c1 = vfmaq_laneq_f32(c1, a1, b1, 1);
    c1 = vfmaq_laneq_f32(c1, a2, b1, 2);
    c1 = vfmaq_laneq_f32(c1, a3, b1, 3);

    c2 = vfmaq_laneq_f32(c2, a0, b2, 0);
    c2 = vfmaq_laneq_f32(c2, a1, b2, 1);
    c2 = vfmaq_laneq_f32(c2, a2, b2, 2);
    c2 = vfmaq_laneq_f32(c2, a3, b2, 3);

    c3 = vfmaq_laneq_f32(c3, a0, b3, 0);
    c3 = vfmaq_laneq_f32(c3, a1, b3, 1);
    c3 = vfmaq_laneq_f32(c3, a2, b3, 2);
    c3 = vfmaq_laneq_f32(c3, a3, b3, 3);

    vst1q_f32(M[0], c0);
    vst1q_f32(M[1], c1);
    vst1q_f32(M[2], c2);
    vst1q_f32(M[3], c3);
}
#else /* !USE_SIMD */
LINMATH_H_FUNC void mat4x4_mul(mat4x4 M, mat4x4 a, mat4x4 b)
{
	mat4x4 temp;
	int k, r, c;
	for(c=0; c<4; ++c) for(r=0; r<4; ++r) {
		temp[c][r] = 0.f;
		for(k=0; k<4; ++k)
			temp[c][r] += a[k][r] * b[c][k];
	}
	mat4x4_dup(M, temp);
}
#endif /* !USE_SIMD */
LINMATH_H_FUNC void mat4x4_translate(mat4x4 T, float x, float y, float z)
{
	mat4x4_identity(T);
	T[3][0] = x;
	T[3][1] = y;
	T[3][2] = z;
}
LINMATH_H_FUNC void mat4x4_translate_in_place(mat4x4 M, float x, float y, float z)
{
	vec4 t = {x, y, z, 0};
	vec4 r;
	int i;
	for (i = 0; i < 4; ++i) {
		mat4x4_row(r, M, i);
		M[3][i] += vec4_mul_inner(r, t);
	}
}
LINMATH_H_FUNC void mat4x4_from_vec3_mul_outer(mat4x4 M, vec3 a, vec3 b)
{
	int i, j;
	for(i=0; i<4; ++i) for(j=0; j<4; ++j)
		M[i][j] = i<3 && j<3 ? a[i] * b[j] : 0.f;
}
LINMATH_H_FUNC void mat4x4_rotate(mat4x4 R, mat4x4 M, float x, float y, float z, float angle)
{
	float s = sinf(angle);
	float c = cosf(angle);
	vec3 u = {x, y, z};

	if(vec3_len(u) > 1e-4) {
		vec3_norm(u, u);
		mat4x4 T;
		mat4x4_from_vec3_mul_outer(T, u, u);

		mat4x4 S = {
			{    0,  u[2], -u[1], 0},
			{-u[2],     0,  u[0], 0},
			{ u[1], -u[0],     0, 0},
			{    0,     0,     0, 0}
		};
		mat4x4_scale(S, S, s);

		mat4x4 C;
		mat4x4_identity(C);
		mat4x4_sub(C, C, T);

		mat4x4_scale(C, C, c);

		mat4x4_add(T, T, C);
		mat4x4_add(T, T, S);

		T[3][3] = 1.;		
		mat4x4_mul(R, M, T);
	} else {
		mat4x4_dup(R, M);
	}
}
LINMATH_H_FUNC void mat4x4_rotate_X(mat4x4 Q, mat4x4 M, float angle)
{
	float s = sinf(angle);
	float c = cosf(angle);
	mat4x4 R = {
		{1.f, 0.f, 0.f, 0.f},
		{0.f,   c,   s, 0.f},
		{0.f,  -s,   c, 0.f},
		{0.f, 0.f, 0.f, 1.f}
	};
	mat4x4_mul(Q, M, R);
}
LINMATH_H_FUNC void mat4x4_rotate_Y(mat4x4 Q, mat4x4 M, float angle)
{
	float s = sinf(angle);
	float c = cosf(angle);
	mat4x4 R = {
		{   c, 0.f,  -s, 0.f},
		{ 0.f, 1.f, 0.f, 0.f},
		{   s, 0.f,   c, 0.f},
		{ 0.f, 0.f, 0.f, 1.f}
	};
	mat4x4_mul(Q, M, R);
}
LINMATH_H_FUNC void mat4x4_rotate_Z(mat4x4 Q, mat4x4 M, float angle)
{
	float s = sinf(angle);
	float c = cosf(angle);
	mat4x4 R = {
		{   c,   s, 0.f, 0.f},
		{  -s,   c, 0.f, 0.f},
		{ 0.f, 0.f, 1.f, 0.f},
		{ 0.f, 0.f, 0.f, 1.f}
	};
	mat4x4_mul(Q, M, R);
}
LINMATH_H_FUNC void mat4x4_invert(mat4x4 T, mat4x4 M)
{
	float s[6];
	float c[6];
	s[0] = M[0][0]*M[1][1] - M[1][0]*M[0][1];
	s[1] = M[0][0]*M[1][2] - M[1][0]*M[0][2];
	s[2] = M[0][0]*M[1][3] - M[1][0]*M[0][3];
	s[3] = M[0][1]*M[1][2] - M[1][1]*M[0][2];
	s[4] = M[0][1]*M[1][3] - M[1][1]*M[0][3];
	s[5] = M[0][2]*M[1][3] - M[1][2]*M[0][3];

	c[0] = M[2][0]*M[3][1] - M[3][0]*M[2][1];
	c[1] = M[2][0]*M[3][2] - M[3][0]*M[2][2];
	c[2] = M[2][0]*M[3][3] - M[3][0]*M[2][3];
	c[3] = M[2][1]*M[3][2] - M[3][1]*M[2][2];
	c[4] = M[2][1]*M[3][3] - M[3][1]*M[2][3];
	c[5] = M[2][2]*M[3][3] - M[3][2]*M[2][3];
	
	/* Assumes it is invertible */
	float idet = 1.0f/( s[0]*c[5]-s[1]*c[4]+s[2]*c[3]+s[3]*c[2]-s[4]*c[1]+s[5]*c[0] );
	
	T[0][0] = ( M[1][1] * c[5] - M[1][2] * c[4] + M[1][3] * c[3]) * idet;
	T[0][1] = (-M[0][1] * c[5] + M[0][2] * c[4] - M[0][3] * c[3]) * idet;
	T[0][2] = ( M[3][1] * s[5] - M[3][2] * s[4] + M[3][3] * s[3]) * idet;
	T[0][3] = (-M[2][1] * s[5] + M[2][2] * s[4] - M[2][3] * s[3]) * idet;

	T[1][0] = (-M[1][0] * c[5] + M[1][2] * c[2] - M[1][3] * c[1]) * idet;
	T[1][1] = ( M[0][0] * c[5] - M[0][2] * c[2] + M[0][3] * c[1]) * idet;
	T[1][2] = (-M[3][0] * s[5] + M[3][2] * s[2] - M[3][3] * s[1]) * idet;
	T[1][3] = ( M[2][0] * s[5] - M[2][2] * s[2] + M[2][3] * s[1]) * idet;

	T[2][0] = ( M[1][0] * c[4] - M[1][1] * c[2] + M[1][3] * c[0]) * idet;
	T[2][1] = (-M[0][0] * c[4] + M[0][1] * c[2] - M[0][3] * c[0]) * idet;
	T[2][2] = ( M[3][0] * s[4] - M[3][1] * s[2] + M[3][3] * s[0]) * idet;
	T[2][3] = (-M[2][0] * s[4] + M[2][1] * s[2] - M[2][3] * s[0]) * idet;

	T[3][0] = (-M[1][0] * c[3] + M[1][1] * c[1] - M[1][2] * c[0]) * idet;
	T[3][1] = ( M[0][0] * c[3] - M[0][1] * c[1] + M[0][2] * c[0]) * idet;
	T[3][2] = (-M[3][0] * s[3] + M[3][1] * s[1] - M[3][2] * s[0]) * idet;
	T[3][3] = ( M[2][0] * s[3] - M[2][1] * s[1] + M[2][2] * s[0]) * idet;
}
LINMATH_H_FUNC void mat4x4_orthonormalize(mat4x4 R, mat4x4 M)
{
	mat4x4_dup(R, M);
	float s = 1.;
	vec3 h;

	vec3_norm(R[2], R[2]);
	
	s = vec3_mul_inner(R[1], R[2]);
	vec3_scale(h, R[2], s);
	vec3_sub(R[1], R[1], h);
	vec3_norm(R[1], R[1]);

	s = vec3_mul_inner(R[0], R[2]);
	vec3_scale(h, R[2], s);
	vec3_sub(R[0], R[0], h);

	s = vec3_mul_inner(R[0], R[1]);
	vec3_scale(h, R[1], s);
	vec3_sub(R[0], R[0], h);
	vec3_norm(R[0], R[0]);
}

LINMATH_H_FUNC void mat4x4_frustum(mat4x4 M, float l, float r, float b, float t, float n, float f)
{
	M[0][0] = 2.f*n/(r-l);
	M[0][1] = M[0][2] = M[0][3] = 0.f;
	
	M[1][1] = 2.*n/(t-b);
	M[1][0] = M[1][2] = M[1][3] = 0.f;

	M[2][0] = (r+l)/(r-l);
	M[2][1] = (t+b)/(t-b);
	M[2][2] = -(f+n)/(f-n);
	M[2][3] = -1.f;
	
	M[3][2] = -2.f*(f*n)/(f-n);
	M[3][0] = M[3][1] = M[3][3] = 0.f;
}
#ifndef CONFIG_NDC_ZERO_ONE
LINMATH_H_FUNC void mat4x4_ortho(mat4x4 M, float l, float r, float b, float t, float n, float f)
{
	M[0][0] = 2.f/(r-l);
	M[0][1] = M[0][2] = M[0][3] = 0.f;

	M[1][1] = 2.f/(t-b);
	M[1][0] = M[1][2] = M[1][3] = 0.f;

	M[2][2] = -2.f/(f-n);
	M[2][0] = M[2][1] = M[2][3] = 0.f;
	
	M[3][0] = -(r+l)/(r-l);
	M[3][1] = -(t+b)/(t-b);
	M[3][2] = -(f+n)/(f-n);
	M[3][3] = 1.f;
}
LINMATH_H_FUNC void mat4x4_perspective(mat4x4 m, float y_fov, float aspect, float n, float f)
{
	/* NOTE: Degrees are an unhandy unit to work with.
	 * linmath.h uses radians for everything! */
	float const a = 1.f / tan(y_fov / 2.f);

	m[0][0] = a / aspect;
	m[0][1] = 0.f;
	m[0][2] = 0.f;
	m[0][3] = 0.f;

	m[1][0] = 0.f;
	m[1][1] = a;
	m[1][2] = 0.f;
	m[1][3] = 0.f;

	m[2][0] = 0.f;
	m[2][1] = 0.f;
	m[2][2] = -((f + n) / (f - n));
	m[2][3] = -1.f;

	m[3][0] = 0.f;
	m[3][1] = 0.f;
	m[3][2] = -((2.f * f * n) / (f - n));
	m[3][3] = 0.f;
}
#else
LINMATH_H_FUNC void mat4x4_ortho(mat4x4 M, float l, float r, float b, float t, float n, float f)
{
	M[0][0] = 2.f/(r-l);
	M[0][1] = M[0][2] = M[0][3] = 0.f;

	M[1][1] = 2.f/(t-b);
	M[1][0] = M[1][2] = M[1][3] = 0.f;

	M[2][2] = -1.f/(f-n);
	M[2][0] = M[2][1] = M[2][3] = 0.f;

	M[3][0] = -(r+l)/(r-l);
	M[3][1] = -(t+b)/(t-b);
	M[3][2] = -(n)/(f-n);
	M[3][3] = 1.f;
}
LINMATH_H_FUNC void mat4x4_perspective(mat4x4 m, float y_fov, float aspect, float n, float f)
{
	float const a = 1.f / tan(y_fov / 2.f);

	m[0][0] = a / aspect;
	m[0][1] = 0.f;
	m[0][2] = 0.f;
	m[0][3] = 0.f;

	m[1][0] = 0.f;
	m[1][1] = a;
	m[1][2] = 0.f;
	m[1][3] = 0.f;

	m[2][0] = 0.f;
	m[2][1] = 0.f;
	m[2][2] = -((f) / (f - n));
	m[2][3] = -1.f;

	m[3][0] = 0.f;
	m[3][1] = 0.f;
	m[3][2] = -((f * n) / (f - n));
	m[3][3] = 0.f;
}
#endif /* !CONFIG_RENDERER_OPENGL */
LINMATH_H_FUNC void mat4x4_look_at(mat4x4 m, vec3 eye, vec3 center, vec3 up)
{
	/* Adapted from Android's OpenGL Matrix.java.                        */
	/* See the OpenGL GLUT documentation for gluLookAt for a description */
	/* of the algorithm. We implement it in a straightforward way:       */

	/* TODO: The negation of of can be spared by swapping the order of
	 *       operands in the following cross products in the right way. */
	vec3 f;
	vec3_sub(f, center, eye);	
	vec3_norm(f, f);	
	
	vec3 s;
	vec3_mul_cross(s, f, up);
	vec3_norm(s, s);

	vec3 t;
	vec3_mul_cross(t, s, f);

	m[0][0] =  s[0];
	m[0][1] =  t[0];
	m[0][2] = -f[0];
	m[0][3] =   0.f;

	m[1][0] =  s[1];
	m[1][1] =  t[1];
	m[1][2] = -f[1];
	m[1][3] =   0.f;

	m[2][0] =  s[2];
	m[2][1] =  t[2];
	m[2][2] = -f[2];
	m[2][3] =   0.f;

	m[3][0] =  0.f;
	m[3][1] =  0.f;
	m[3][2] =  0.f;
	m[3][3] =  1.f;

	mat4x4_translate_in_place(m, -eye[0], -eye[1], -eye[2]);
}

LINMATH_H_FUNC void mat4x4_look_at_safe(mat4x4 m, vec3 eye, vec3 center, vec3 up)
{
	vec3 forward, up_adj;
	vec3_sub(forward, center, eye);
	vec3_norm(forward, forward);

	/* If forward and up are nearly parallel, pick a different up vector */
	float dp = fabsf(vec3_mul_inner(forward, up));
	if (dp > 0.999f)
		vec3_dup(up_adj, (vec3){ 0.0, 0.0, -1.0 });
	else
	    vec3_dup(up_adj, up);

	mat4x4_look_at(m, eye, center, up_adj);
}

typedef float quat[4];
LINMATH_H_FUNC void quat_identity(quat q)
{
	q[0] = q[1] = q[2] = 0.f;
	q[3] = 1.f;
}
LINMATH_H_FUNC void quat_from_axis_angle(quat r, vec3 axis, float angle)
{
	float l = vec3_mul_inner(axis, axis);
	if (l > 0.0) {
	    float half = angle * 0.5f;
	    l = sinf(half) / sqrtf(l);

	    r[0] = axis[0] * l;
	    r[1] = axis[1] * l;
	    r[2] = axis[2] * l;
	    r[3] = cosf(half);
	} else {
		r[0] = r[1] = r[2] = 0.0;
		r[3] = 1.0;
	}
}
LINMATH_H_FUNC void quat_from_euler_xyz(quat q, float x, float y, float z)
{
    float cx = cosf(x * 0.5f);
    float sx = sinf(x * 0.5f);
    float cy = cosf(y * 0.5f);
    float sy = sinf(y * 0.5f);
    float cz = cosf(z * 0.5f);
    float sz = sinf(z * 0.5f);

    q[0] = sx * cy * cz - cx * sy * sz;
    q[1] = cx * sy * cz + sx * cy * sz;
    q[2] = cx * cy * sz - sx * sy * cz;
    q[3] = cx * cy * cz + sx * sy * sz;
}
LINMATH_H_FUNC void quat_to_euler_xyz(const quat q, float *x, float *y, float *z)
{
    float sinr_cosp = 2.0f * (q[3]*q[0] + q[1]*q[2]);
    float cosr_cosp = 1.0f - 2.0f * (q[0]*q[0] + q[1]*q[1]);
    *x = atan2f(sinr_cosp, cosr_cosp); // X

    float sinp = 2.0f * (q[3]*q[1] - q[2]*q[0]);
    if (fabsf(sinp) >= 1.0f)
        *y = copysignf(M_PI / 2.0f, sinp); // Y
    else
        *y = asinf(sinp); // Y

    float siny_cosp = 2.0f * (q[3]*q[2] + q[0]*q[1]);
    float cosy_cosp = 1.0f - 2.0f * (q[1]*q[1] + q[2]*q[2]);
    *z = atan2f(siny_cosp, cosy_cosp); // Z
}
LINMATH_H_FUNC void quat_add(quat r, quat a, quat b)
{
	int i;
	for(i=0; i<4; ++i)
		r[i] = a[i] + b[i];
}
LINMATH_H_FUNC void quat_sub(quat r, quat a, quat b)
{
	int i;
	for(i=0; i<4; ++i)
		r[i] = a[i] - b[i];
}
LINMATH_H_FUNC void quat_mul(quat r, quat p, quat q)
{
	vec3 w;
	vec3_mul_cross(r, p, q);
	vec3_scale(w, p, q[3]);
	vec3_add(r, r, w);
	vec3_scale(w, q, p[3]);
	vec3_add(r, r, w);
	r[3] = p[3]*q[3] - vec3_mul_inner(p, q);
}
LINMATH_H_FUNC void quat_scale(quat r, quat v, float s)
{
	int i;
	for(i=0; i<4; ++i)
		r[i] = v[i] * s;
}
LINMATH_H_FUNC float quat_inner_product(quat a, quat b)
{
	float p = 0.f;
	int i;
	for(i=0; i<4; ++i)
		p += b[i]*a[i];
	return p;
}
LINMATH_H_FUNC void quat_conj(quat r, quat q)
{
	int i;
	for(i=0; i<3; ++i)
		r[i] = -q[i];
	r[3] = q[3];
}
LINMATH_H_FUNC void quat_rotate(quat r, float angle, vec3 axis) {
	vec3 v;
	vec3_scale(v, axis, sinf(angle / 2));
	int i;
	for(i=0; i<3; ++i)
		r[i] = v[i];
	r[3] = cosf(angle / 2);
}
#define quat_norm vec4_norm
LINMATH_H_FUNC void quat_mul_vec3(vec3 r, quat q, vec3 v)
{
/*
 * Method by Fabian 'ryg' Giessen (of Farbrausch)
t = 2 * cross(q.xyz, v)
v' = v + q.w * t + cross(q.xyz, t)
 */
	vec3 t;
	vec3 q_xyz = {q[0], q[1], q[2]};
	vec3 u = {q[0], q[1], q[2]};

	vec3_mul_cross(t, q_xyz, v);
	vec3_scale(t, t, 2);

	vec3_mul_cross(u, q_xyz, t);
	vec3_scale(t, t, q[3]);

	vec3_add(r, v, t);
	vec3_add(r, r, u);
}
LINMATH_H_FUNC void mat4x4_from_quat(mat4x4 M, quat q)
{
	float a = q[3];
	float b = q[0];
	float c = q[1];
	float d = q[2];
	float a2 = a*a;
	float b2 = b*b;
	float c2 = c*c;
	float d2 = d*d;
	
	M[0][0] = a2 + b2 - c2 - d2;
	M[0][1] = 2.f*(b*c + a*d);
	M[0][2] = 2.f*(b*d - a*c);
	M[0][3] = 0.f;

	M[1][0] = 2*(b*c - a*d);
	M[1][1] = a2 - b2 + c2 - d2;
	M[1][2] = 2.f*(c*d + a*b);
	M[1][3] = 0.f;

	M[2][0] = 2.f*(b*d + a*c);
	M[2][1] = 2.f*(c*d - a*b);
	M[2][2] = a2 - b2 - c2 + d2;
	M[2][3] = 0.f;

	M[3][0] = M[3][1] = M[3][2] = 0.f;
	M[3][3] = 1.f;
}

LINMATH_H_FUNC void mat4x4o_mul_quat(mat4x4 R, mat4x4 M, quat q)
{
/*  XXX: The way this is written only works for othogonal matrices. */
/* TODO: Take care of non-orthogonal case. */
	quat_mul_vec3(R[0], q, M[0]);
	quat_mul_vec3(R[1], q, M[1]);
	quat_mul_vec3(R[2], q, M[2]);

	R[3][0] = R[3][1] = R[3][2] = 0.f;
	R[3][3] = 1.f;
}
LINMATH_H_FUNC void quat_from_mat4x4(quat q, mat4x4 M)
{
	float r=0.f;
	int i;

	int perm[] = { 0, 1, 2, 0, 1 };
	int *p = perm;

	for(i = 0; i<3; i++) {
		float m = M[i][i];
		if( m < r )
			continue;
		m = r;
		p = &perm[i];
	}

	r = sqrtf(1.f + M[p[0]][p[0]] - M[p[1]][p[1]] - M[p[2]][p[2]] );

	if(r < 1e-6) {
		q[0] = 1.f;
		q[1] = q[2] = q[3] = 0.f;
		return;
	}

	q[0] = r/2.f;
	q[1] = (M[p[0]][p[1]] - M[p[1]][p[0]])/(2.f*r);
	q[2] = (M[p[2]][p[0]] - M[p[0]][p[2]])/(2.f*r);
	q[3] = (M[p[2]][p[1]] - M[p[1]][p[2]])/(2.f*r);
}

LINMATH_H_FUNC void mat4x4_arcball(mat4x4 R, mat4x4 M, vec2 _a, vec2 _b, float s)
{
	vec2 a; memcpy(a, _a, sizeof(a));
	vec2 b; memcpy(b, _b, sizeof(b));
	
	float z_a = 0.;
	float z_b = 0.;

	if(vec2_len(a) < 1.) {
		z_a = sqrtf(1. - vec2_mul_inner(a, a));
	} else {
		vec2_norm(a, a);
	}

	if(vec2_len(b) < 1.) {
		z_b = sqrtf(1. - vec2_mul_inner(b, b));
	} else {
		vec2_norm(b, b);
	}
	
	vec3 a_ = {a[0], a[1], z_a};
	vec3 b_ = {b[0], b[1], z_b};

	vec3 c_;
	vec3_mul_cross(c_, a_, b_);

	float const angle = acos(vec3_mul_inner(a_, b_)) * s;
	mat4x4_rotate(R, M, c_[0], c_[1], c_[2], angle);
}
#endif
