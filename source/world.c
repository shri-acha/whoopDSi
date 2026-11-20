#include "world.h"

Box  worldBoxes[MAX_BOXES];
int  worldNumBoxes;
Gate worldGates[MAX_GATES];
int  worldNumGates;
Vec3 worldStart;

#define RESTITUTION 0.25f
#define FRICTION    0.75f   /* tangential velocity kept per contact */

static void box(float x0, float y0, float z0, float x1, float y1, float z1,
                unsigned char r, unsigned char g, unsigned char b, bool collide)
{
    if (worldNumBoxes >= MAX_BOXES) return;
    Box *bx = &worldBoxes[worldNumBoxes++];
    bx->mn = v3(x0, y0, z0);
    bx->mx = v3(x1, y1, z1);
    bx->r = r; bx->g = g; bx->b = b;
    bx->collide = collide;
    bx->gate = -1;
}

#define SOLID(x0,y0,z0,x1,y1,z1,r,g,b) box(x0,y0,z0,x1,y1,z1,r,g,b,true)
#define DECAL(x0,y0,z0,x1,y1,z1,r,g,b) box(x0,y0,z0,x1,y1,z1,r,g,b,false)

/* A square race gate: 4 bars around the opening plus a pole to the floor. */
static void gate(float cx, float cy, float cz, int axis, int dir)
{
    if (worldNumGates >= MAX_GATES) return;
    int gi = worldNumGates++;
    Gate *g = &worldGates[gi];
    g->c = v3(cx, cy, cz);
    g->axis = axis;
    g->dir = dir;
    g->half = 0.28f;

    const float h = g->half, t = 0.035f, d = 0.02f; /* bar thickness, half depth */
    int first = worldNumBoxes;
    /* local (u = across, v = up), plane coordinate w */
    #define GBAR(u0, v0, u1, v1) \
        if (axis == 2) SOLID(cx + (u0), cy + (v0), cz - d, cx + (u1), cy + (v1), cz + d, 255, 255, 255); \
        else           SOLID(cx - d, cy + (v0), cz + (u0), cx + d, cy + (v1), cz + (u1), 255, 255, 255)
    GBAR(-h - t, -h - t,  h + t, -h);      /* bottom */
    GBAR(-h - t,  h,      h + t,  h + t);  /* top    */
    GBAR(-h - t, -h,     -h,      h);      /* left   */
    GBAR( h,     -h,      h + t,  h);      /* right  */
    /* pole / stand down to the floor */
    float poleTop = -h - t;
    float poleH = cy + poleTop;
    if (poleH > 0.02f) {
        GBAR(-0.012f, -cy, 0.012f, poleTop);
    }
    #undef GBAR
    for (int i = first; i < worldNumBoxes; i++)
        worldBoxes[i].gate = (signed char)gi;
    /* little foot so the stand looks grounded */
    DECAL(cx - 0.12f, 0.0f, cz - 0.12f, cx + 0.12f, 0.015f, cz + 0.12f, 60, 60, 60);
}

void worldInit(void)
{
    worldNumBoxes = 0;
    worldNumGates = 0;

    worldStart = v3(1.2f, QUAD_RADIUS, 1.6f);

    /* ---------- decals (no collision) ---------- */
    DECAL(-1.4f, 0.0f, -0.2f, 0.6f, 0.006f, 1.3f, 150, 40, 50);       /* rug          */
    DECAL(-1.25f, 0.0f, -0.05f, 0.45f, 0.008f, 1.15f, 200, 170, 90);  /* rug border   */
    DECAL(0.95f, 0.0f, 1.35f, 1.45f, 0.008f, 1.85f, 40, 150, 210);     /* start pad    */
    DECAL(1.02f, 0.0f, 1.42f, 1.38f, 0.01f, 1.78f, 45, 50, 60);       /* pad centre   */
    DECAL(-1.0f, 0.8f, -2.5f, 0.2f, 1.8f, -2.49f, 150, 210, 255);      /* window       */
    DECAL(-1.05f, 0.75f, -2.5f, 0.25f, 0.8f, -2.48f, 240, 240, 240);   /* sill         */
    DECAL(2.99f, 0.0f, -0.6f, 3.0f, 2.0f, 0.3f, 120, 75, 40);          /* door         */
    DECAL(2.97f, 0.95f, -0.5f, 2.99f, 1.0f, -0.42f, 220, 200, 60);     /* door handle  */
    DECAL(-0.8f, 0.9f, 2.48f, 0.4f, 1.6f, 2.5f, 20, 20, 25);           /* TV           */
    DECAL(-3.0f, 1.3f, -1.6f, -2.99f, 1.9f, -0.8f, 240, 150, 60);      /* painting     */

    /* ---------- furniture ---------- */
    /* coffee/dining table */
    SOLID(-1.6f, 0.70f, -1.2f, -0.4f, 0.76f, -0.4f, 170, 110, 60);
    SOLID(-1.58f, 0.0f, -1.18f, -1.52f, 0.70f, -1.12f, 120, 80, 45);
    SOLID(-0.48f, 0.0f, -1.18f, -0.42f, 0.70f, -1.12f, 120, 80, 45);
    SOLID(-1.58f, 0.0f, -0.48f, -1.52f, 0.70f, -0.42f, 120, 80, 45);
    SOLID(-0.48f, 0.0f, -0.48f, -0.42f, 0.70f, -0.42f, 120, 80, 45);
    /* couch against the back wall */
    SOLID(0.5f, 0.0f, -2.5f, 2.6f, 0.45f, -1.7f, 70, 90, 140);
    SOLID(0.5f, 0.45f, -2.5f, 2.6f, 0.9f, -2.2f, 60, 80, 125);
    SOLID(0.5f, 0.45f, -2.2f, 0.7f, 0.65f, -1.7f, 60, 80, 125);
    SOLID(2.4f, 0.45f, -2.2f, 2.6f, 0.65f, -1.7f, 60, 80, 125);
    /* bookshelf on the left wall */
    SOLID(-3.0f, 0.0f, 0.2f, -2.65f, 1.9f, 1.6f, 140, 95, 55);
    DECAL(-2.66f, 0.45f, 0.25f, -2.64f, 0.85f, 1.55f, 60, 120, 70);   /* books */
    DECAL(-2.66f, 1.0f, 0.25f, -2.64f, 1.4f, 1.55f, 170, 60, 60);
    /* bed in the front-right corner */
    SOLID(1.6f, 0.0f, 0.8f, 3.0f, 0.5f, 2.5f, 220, 220, 230);
    SOLID(2.85f, 0.5f, 0.8f, 3.0f, 1.1f, 2.5f, 110, 70, 50);          /* headboard */
    SOLID(2.3f, 0.5f, 1.0f, 2.8f, 0.62f, 1.5f, 250, 250, 255);        /* pillow    */
    /* stacked cardboard boxes */
    SOLID(-0.3f, 0.0f, 0.6f, 0.1f, 0.35f, 1.0f, 190, 150, 90);
    SOLID(-0.25f, 0.35f, 0.65f, 0.05f, 0.6f, 0.95f, 175, 135, 80);
    /* ceiling lamp */
    SOLID(-0.01f, 2.25f, -0.01f, 0.01f, ROOM_H, 0.01f, 30, 30, 30);
    SOLID(-0.18f, 2.1f, -0.18f, 0.18f, 2.25f, 0.18f, 255, 240, 170);
    /* plant pot in the back-left corner */
    SOLID(-2.9f, 0.0f, -2.4f, -2.5f, 0.4f, -2.0f, 150, 80, 50);
    SOLID(-2.85f, 0.4f, -2.35f, -2.55f, 1.1f, -2.05f, 50, 140, 60);

    /* ---------- race course (counter-clockwise loop) ---------- */
    gate( 1.2f, 0.80f,  0.3f, 2, -1);   /* 1: over the start, toward the couch */
    gate(-0.2f, 1.30f, -1.6f, 0, -1);   /* 2: high gate by the window          */
    gate(-1.9f, 0.90f, -0.2f, 2, +1);   /* 3: beside the table                 */
    gate(-0.4f, 0.50f,  1.6f, 0, +1);   /* 4: low gate in front of the TV      */
}

static float resolveContact(Quad *q, Vec3 n, Vec3 contact)
{
    float impact = 0.0f;
    q->pos = vadd(contact, vscale(n, QUAD_RADIUS));
    float vn = vdot(q->vel, n);
    if (vn < 0.0f) {
        impact = -vn;
        Vec3 vnv = vscale(n, vn);
        Vec3 vt = vsub(q->vel, vnv);
        q->vel = vadd(vscale(vt, FRICTION), vscale(vnv, -RESTITUTION));
        /* hitting things knocks the frame around a bit */
        q->rate = vscale(q->rate, 0.6f);
    }
    return impact;
}

float worldCollide(Quad *q)
{
    float maxImpact = 0.0f, imp;
    Vec3 p = q->pos;
    const float r = QUAD_RADIUS;

    /* room shell */
    if (p.y < r) {
        imp = resolveContact(q, v3(0, 1, 0), v3(p.x, 0.0f, p.z));
        if (imp > maxImpact) maxImpact = imp;
        /* resting on the floor with motors low: settle down */
        if (!q->armed || q->thrustOut < 0.3f) {
            q->vel.x *= 0.85f;
            q->vel.z *= 0.85f;
            q->rate = vscale(q->rate, 0.7f);
        }
    }
    p = q->pos;
    if (p.y > ROOM_H - r) { imp = resolveContact(q, v3(0, -1, 0), v3(p.x, ROOM_H, p.z)); if (imp > maxImpact) maxImpact = imp; }
    p = q->pos;
    if (p.x < ROOM_MIN_X + r) { imp = resolveContact(q, v3(1, 0, 0), v3(ROOM_MIN_X, p.y, p.z)); if (imp > maxImpact) maxImpact = imp; }
    p = q->pos;
    if (p.x > ROOM_MAX_X - r) { imp = resolveContact(q, v3(-1, 0, 0), v3(ROOM_MAX_X, p.y, p.z)); if (imp > maxImpact) maxImpact = imp; }
    p = q->pos;
    if (p.z < ROOM_MIN_Z + r) { imp = resolveContact(q, v3(0, 0, 1), v3(p.x, p.y, ROOM_MIN_Z)); if (imp > maxImpact) maxImpact = imp; }
    p = q->pos;
    if (p.z > ROOM_MAX_Z - r) { imp = resolveContact(q, v3(0, 0, -1), v3(p.x, p.y, ROOM_MAX_Z)); if (imp > maxImpact) maxImpact = imp; }

    /* furniture and gates: sphere vs AABB */
    for (int i = 0; i < worldNumBoxes; i++) {
        const Box *b = &worldBoxes[i];
        if (!b->collide) continue;
        p = q->pos;
        Vec3 c;
        c.x = p.x < b->mn.x ? b->mn.x : (p.x > b->mx.x ? b->mx.x : p.x);
        c.y = p.y < b->mn.y ? b->mn.y : (p.y > b->mx.y ? b->mx.y : p.y);
        c.z = p.z < b->mn.z ? b->mn.z : (p.z > b->mx.z ? b->mx.z : p.z);
        Vec3 d = vsub(p, c);
        float d2 = vdot(d, d);
        if (d2 >= r * r) continue;

        Vec3 n;
        if (d2 > 1e-8f) {
            n = vscale(d, 1.0f / sqrtf(d2));
        } else {
            /* centre inside the box: push out along the shallowest axis */
            float pen[6] = {
                p.x - b->mn.x, b->mx.x - p.x,
                p.y - b->mn.y, b->mx.y - p.y,
                p.z - b->mn.z, b->mx.z - p.z,
            };
            int best = 0;
            for (int k = 1; k < 6; k++) if (pen[k] < pen[best]) best = k;
            static const Vec3 normals[6] = {
                { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 },
            };
            n = normals[best];
            if (best == 0) c.x = b->mn.x; else if (best == 1) c.x = b->mx.x;
            else if (best == 2) c.y = b->mn.y; else if (best == 3) c.y = b->mx.y;
            else if (best == 4) c.z = b->mn.z; else c.z = b->mx.z;
        }
        imp = resolveContact(q, n, c);
        if (imp > maxImpact) maxImpact = imp;
    }
    return maxImpact;
}

bool worldGateCrossed(int gi, Vec3 a, Vec3 b)
{
    const Gate *g = &worldGates[gi];
    float wa, wb;
    if (g->axis == 2) { wa = (a.z - g->c.z) * g->dir; wb = (b.z - g->c.z) * g->dir; }
    else              { wa = (a.x - g->c.x) * g->dir; wb = (b.x - g->c.x) * g->dir; }
    if (!(wa < 0.0f && wb >= 0.0f)) return false;

    float t = wa / (wa - wb);
    Vec3 p = vadd(a, vscale(vsub(b, a), t));
    float u = (g->axis == 2) ? p.x - g->c.x : p.z - g->c.z;
    float v = p.y - g->c.y;
    return u > -g->half && u < g->half && v > -g->half && v < g->half;
}
