#ifndef VMATH_H
#define VMATH_H

#include <math.h>

typedef struct { float x, y, z; } Vec3;

/* Rotation matrix stored as three column vectors (body axes in world space):
 *   r = body right (+X), u = body up (+Y), b = body back (+Z).
 * The quad's nose / camera points along -b. */
typedef struct { Vec3 r, u, b; } Mat3;

static inline Vec3 v3(float x, float y, float z) { Vec3 v = { x, y, z }; return v; }
static inline Vec3 vadd(Vec3 a, Vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline Vec3 vsub(Vec3 a, Vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline Vec3 vscale(Vec3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float vdot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3 vcross(Vec3 a, Vec3 b)
{
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline float vlen(Vec3 a) { return sqrtf(vdot(a, a)); }
static inline Vec3 vnorm(Vec3 a)
{
    float l = vlen(a);
    return l > 1e-6f ? vscale(a, 1.0f / l) : a;
}

/* world = M * body */
static inline Vec3 mmul(const Mat3 *m, Vec3 v)
{
    return vadd(vadd(vscale(m->r, v.x), vscale(m->u, v.y)), vscale(m->b, v.z));
}
/* body = M^T * world */
static inline Vec3 mtmul(const Mat3 *m, Vec3 v)
{
    return v3(vdot(m->r, v), vdot(m->u, v), vdot(m->b, v));
}

#endif
