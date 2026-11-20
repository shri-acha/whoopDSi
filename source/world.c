#include "world.h"

Box   worldBoxes[MAX_BOXES];
int   worldNumBoxes;
Patch worldPatches[MAX_PATCHES];
int   worldNumPatches;
Gate  worldGates[MAX_GATES];
int   worldNumGates;
Vec3  worldStart;

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
#define DECO(x0,y0,z0,x1,y1,z1,r,g,b)  box(x0,y0,z0,x1,y1,z1,r,g,b,false)

/* Flat patch on the floor or a wall. Later patches paint over earlier ones. */
static void patch(Surface s, float u0, float v0, float u1, float v1,
                  unsigned char r, unsigned char g, unsigned char b)
{
    if (worldNumPatches >= MAX_PATCHES) return;
    Patch *p = &worldPatches[worldNumPatches++];
    p->surf = s;
    p->u0 = u0; p->v0 = v0; p->u1 = u1; p->v1 = v1;
    p->r = r; p->g = g; p->b = b;
}

/* A square race gate: 4 bars around the opening plus a pole to the floor. */
static void gate(float cx, float cy, float cz, int axis, int dir, float half)
{
    if (worldNumGates >= MAX_GATES) return;
    int gi = worldNumGates++;
    Gate *g = &worldGates[gi];
    g->c = v3(cx, cy, cz);
    g->axis = axis;
    g->dir = dir;
    g->half = half;

    const float h = half, t = half * 0.125f, d = half * 0.07f; /* bar thickness, half depth */
    const float pw = half * 0.045f;                            /* half pole width          */
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
        GBAR(-pw, -cy, pw, poleTop);
    }
    #undef GBAR
    for (int i = first; i < worldNumBoxes; i++)
        worldBoxes[i].gate = (signed char)gi;
    /* little foot so the stand looks grounded */
    float f = half * 0.43f;
    patch(SURF_FLOOR, cx - f, cz - f, cx + f, cz + f, 60, 60, 60);
}

/* Start pad: blue square with a dark centre, centred on worldStart. */
static void startPad(float x, float z)
{
    worldStart = v3(x, QUAD_RADIUS, z);
    patch(SURF_FLOOR, x - 0.25f, z - 0.25f, x + 0.25f, z + 0.25f, 40, 150, 210);
    patch(SURF_FLOOR, x - 0.18f, z - 0.18f, x + 0.18f, z + 0.18f, 45, 50, 60);
}

const MapInfo mapInfo[MAP_COUNT] = {
    [MAP_INDOOR] = {
        .name = "INDOOR", .desc = "Living room, 6 x 5 m",
        .minX = -3.0f, .maxX = 3.0f, .minZ = -2.5f, .maxZ = 2.5f,
        .wallH = 2.6f, .ceilH = 2.6f, .indoor = true,
    },
    [MAP_OUTDOOR] = {
        .name = "OUTDOOR", .desc = "Back garden, 24 x 20 m",
        .minX = -12.0f, .maxX = 12.0f, .minZ = -10.0f, .maxZ = 10.0f,
        .wallH = 2.4f, .ceilH = 9.0f, .indoor = false,
    },
};

MapId worldMap;
const MapInfo *world = &mapInfo[MAP_INDOOR];

static void buildIndoor(void)
{
    const MapInfo *m = world;

    /* ---------- floor patches ---------- */
    patch(SURF_FLOOR, -1.4f, -0.2f, 0.6f, 1.3f, 150, 40, 50);        /* rug          */
    patch(SURF_FLOOR, -1.25f, -0.05f, 0.45f, 1.15f, 200, 170, 90);   /* rug centre   */
    startPad(1.2f, 1.6f);

    /* ---------- wall patches ---------- */
    /* skirting boards first, so the door can cut through them */
    patch(SURF_BACK,  m->minX, 0.0f, m->maxX, 0.08f, 235, 235, 235);
    patch(SURF_FRONT, m->minX, 0.0f, m->maxX, 0.08f, 235, 235, 235);
    patch(SURF_LEFT,  m->minZ, 0.0f, m->maxZ, 0.08f, 235, 235, 235);
    patch(SURF_RIGHT, m->minZ, 0.0f, m->maxZ, 0.08f, 235, 235, 235);
    /* window on the back wall: white frame, sky glass, cross mullion */
    patch(SURF_BACK, -1.06f, 0.74f, 0.26f, 1.86f, 245, 245, 245);
    patch(SURF_BACK, -1.0f, 0.8f, 0.2f, 1.8f, 150, 210, 255);
    patch(SURF_BACK, -0.43f, 0.8f, -0.37f, 1.8f, 245, 245, 245);
    patch(SURF_BACK, -1.0f, 1.27f, 0.2f, 1.33f, 245, 245, 245);
    /* door on the right wall: frame, door leaf, two recessed panels */
    patch(SURF_RIGHT, -0.66f, 0.0f, 0.36f, 2.06f, 235, 235, 235);
    patch(SURF_RIGHT, -0.6f, 0.0f, 0.3f, 2.0f, 120, 75, 40);
    patch(SURF_RIGHT, -0.5f, 1.1f, 0.2f, 1.85f, 100, 62, 32);
    patch(SURF_RIGHT, -0.5f, 0.15f, 0.2f, 0.9f, 100, 62, 32);
    /* painting on the left wall */
    patch(SURF_LEFT, -1.65f, 1.25f, -0.75f, 1.95f, 90, 60, 35);
    patch(SURF_LEFT, -1.6f, 1.3f, -0.8f, 1.9f, 240, 150, 60);

    /* ---------- objects sticking out of the walls ---------- */
    DECO(-1.05f, 0.75f, m->minZ, 0.25f, 0.8f, m->minZ + 0.05f, 240, 240, 240); /* sill   */
    DECO(m->maxX - 0.04f, 0.95f, -0.5f, m->maxX, 1.0f, -0.42f, 220, 200, 60);  /* handle */
    SOLID(-0.8f, 0.9f, m->maxZ - 0.04f, 0.4f, 1.6f, m->maxZ, 20, 20, 25);     /* TV     */

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
    /* bookshelf on the left wall, rows of books standing proud of it */
    SOLID(-3.0f, 0.0f, 0.2f, -2.65f, 1.9f, 1.6f, 140, 95, 55);
    DECO(-2.66f, 0.45f, 0.25f, -2.62f, 0.85f, 1.55f, 60, 120, 70);
    DECO(-2.66f, 1.0f, 0.25f, -2.62f, 1.4f, 1.55f, 170, 60, 60);
    /* bed in the front-right corner */
    SOLID(1.6f, 0.0f, 0.8f, 3.0f, 0.5f, 2.5f, 220, 220, 230);
    SOLID(2.85f, 0.5f, 0.8f, 3.0f, 1.1f, 2.5f, 110, 70, 50);          /* headboard */
    SOLID(2.3f, 0.5f, 1.0f, 2.8f, 0.62f, 1.5f, 250, 250, 255);        /* pillow    */
    /* stacked cardboard boxes */
    SOLID(-0.3f, 0.0f, 0.6f, 0.1f, 0.35f, 1.0f, 190, 150, 90);
    SOLID(-0.25f, 0.35f, 0.65f, 0.05f, 0.6f, 0.95f, 175, 135, 80);
    /* ceiling lamp */
    SOLID(-0.01f, 2.25f, -0.01f, 0.01f, m->ceilH, 0.01f, 30, 30, 30);
    SOLID(-0.18f, 2.1f, -0.18f, 0.18f, 2.25f, 0.18f, 255, 240, 170);
    /* plant pot in the back-left corner */
    SOLID(-2.9f, 0.0f, -2.4f, -2.5f, 0.4f, -2.0f, 150, 80, 50);
    SOLID(-2.85f, 0.4f, -2.35f, -2.55f, 1.1f, -2.05f, 50, 140, 60);

    /* ---------- race course (counter-clockwise loop) ---------- */
    gate( 1.2f, 0.80f,  0.3f, 2, -1, 0.28f);   /* 1: over the start, toward the couch */
    gate(-0.2f, 1.30f, -1.6f, 0, -1, 0.28f);   /* 2: high gate by the window          */
    gate(-1.9f, 0.90f, -0.2f, 2, +1, 0.28f);   /* 3: beside the table                 */
    gate(-0.4f, 0.50f,  1.6f, 0, +1, 0.28f);   /* 4: low gate in front of the TV      */
}

static void tree(float x, float z, float s)
{
    SOLID(x - 0.15f * s, 0.0f, z - 0.15f * s, x + 0.15f * s, 2.0f * s, z + 0.15f * s, 110, 75, 45);
    SOLID(x - 1.1f * s, 1.7f * s, z - 1.1f * s, x + 1.1f * s, 3.0f * s, z + 1.1f * s, 50, 120, 50);
    SOLID(x - 0.7f * s, 3.0f * s, z - 0.7f * s, x + 0.7f * s, 3.8f * s, z + 0.7f * s, 65, 145, 55);
}

static void tyreStack(float x, float z)
{
    SOLID(x - 0.3f, 0.0f, z - 0.3f, x + 0.3f, 0.2f, z + 0.3f, 35, 35, 38);
    SOLID(x - 0.3f, 0.2f, z - 0.3f, x + 0.3f, 0.4f, z + 0.3f, 230, 60, 40);
    SOLID(x - 0.3f, 0.4f, z - 0.3f, x + 0.3f, 0.6f, z + 0.3f, 35, 35, 38);
}

static void buildOutdoor(void)
{
    /* ---------- ground patches ---------- */
    patch(SURF_FLOOR, -4.4f, -4.4f, 0.4f, -0.6f, 120, 100, 70);       /* pond bank  */
    patch(SURF_FLOOR, -4.0f, -4.0f, 0.0f, -1.0f, 50, 110, 170);       /* pond       */
    patch(SURF_FLOOR, 3.2f, 6.5f, 7.8f, 10.0f, 165, 145, 110);        /* patio      */
    startPad(6.3f, 8.5f);

    /* ---------- scenery ---------- */
    tree(-9.0f, -7.0f, 1.0f);
    tree(-10.0f, 2.0f, 1.2f);
    tree(9.5f, -8.0f, 1.1f);
    tree(10.0f, 2.5f, 1.0f);
    tree(1.5f, -1.5f, 1.3f);       /* big tree in the middle of the loop */
    tree(-9.0f, 8.0f, 0.9f);
    tree(-3.0f, 5.0f, 0.8f);
    /* garden shed in the back-left corner */
    SOLID(-11.5f, 0.0f, -9.7f, -8.5f, 2.0f, -7.5f, 150, 60, 50);
    SOLID(-11.7f, 2.0f, -9.9f, -8.3f, 2.2f, -7.3f, 70, 70, 75);
    /* picnic table on the patio */
    SOLID(3.6f, 0.72f, 7.2f, 5.2f, 0.78f, 8.0f, 160, 110, 65);
    SOLID(3.7f, 0.0f, 7.3f, 3.8f, 0.72f, 7.9f, 120, 80, 45);
    SOLID(5.0f, 0.0f, 7.3f, 5.1f, 0.72f, 7.9f, 120, 80, 45);
    SOLID(3.6f, 0.42f, 6.8f, 5.2f, 0.47f, 7.05f, 140, 95, 55);       /* bench */
    /* tyre stacks marking the turns */
    tyreStack(7.5f, -6.0f);
    tyreStack(-8.0f, -5.5f);
    tyreStack(-7.5f, 6.0f);
    /* washing line poles */
    SOLID(-5.0f, 0.0f, 8.5f, -4.92f, 2.0f, 8.58f, 200, 200, 205);
    SOLID(1.0f, 0.0f, 8.5f, 1.08f, 2.0f, 8.58f, 200, 200, 205);

    /* ---------- race course (counter-clockwise loop, bigger gates) ---------- */
    gate( 6.3f, 1.0f,  5.0f, 2, -1, 0.5f);   /* 1: off the patio                   */
    gate( 6.0f, 2.6f, -3.0f, 2, -1, 0.5f);   /* 2: high gate                        */
    gate( 0.0f, 1.0f, -7.0f, 0, -1, 0.5f);   /* 3: along the back hedge             */
    gate(-7.0f, 1.6f, -2.0f, 2, +1, 0.5f);   /* 4: beside the pond                  */
    gate(-6.0f, 0.7f,  4.0f, 2, +1, 0.5f);   /* 5: low gate                         */
    gate(-1.0f, 1.2f,  7.0f, 0, +1, 0.5f);   /* 6: past the washing line, home      */
}

void worldInit(MapId map)
{
    worldMap = map;
    world = &mapInfo[map];
    worldNumBoxes = 0;
    worldNumPatches = 0;
    worldNumGates = 0;

    if (map == MAP_OUTDOOR) buildOutdoor();
    else                    buildIndoor();
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
    /* boundary walls and ceiling. Outdoors only the hedge is solid; above it
     * and at the flight ceiling a soft net pushes the quad back without ever
     * counting as a crash. */
    const MapInfo *m = world;
    #define WALL(cond, nx, ny, nz, cx, cy, cz, soft) \
        p = q->pos; \
        if (cond) { \
            imp = resolveContact(q, v3(nx, ny, nz), v3(cx, cy, cz)); \
            if (!(soft) && imp > maxImpact) maxImpact = imp; \
        }
    bool aboveWall = !m->indoor && q->pos.y > m->wallH;
    WALL(p.y > m->ceilH - r, 0, -1, 0, p.x, m->ceilH, p.z, !m->indoor);
    WALL(p.x < m->minX + r,  1, 0, 0, m->minX, p.y, p.z, aboveWall);
    WALL(p.x > m->maxX - r, -1, 0, 0, m->maxX, p.y, p.z, aboveWall);
    WALL(p.z < m->minZ + r,  0, 0, 1, p.x, p.y, m->minZ, aboveWall);
    WALL(p.z > m->maxZ - r,  0, 0, -1, p.x, p.y, m->maxZ, aboveWall);
    #undef WALL

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
