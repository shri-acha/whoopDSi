#include <nds.h>
#include <stdio.h>
#include <string.h>
#include "render.h"
#include "world.h"

/* ------------------------------------------------------------------------ */
/* Static scene, baked once into fixed-point quads with per-vertex colour.  */
/* ------------------------------------------------------------------------ */

#define MAX_RQUADS 1200

typedef struct {
    v16 v[4][3];
    u16 col[4];
    s8  gate;    /* >=0: colour comes from gate state at draw time */
    u8  shade;   /* 0..32 face brightness, used for gate colouring */
} RQuad;

static RQuad rquads[MAX_RQUADS];
static int numRQuads;

/* Vertices are stored as v16 (4.12 fixed, range +-8) in units of
 * WORLD_SCALE metres, and the modelview scales them back up. That keeps
 * ~1 mm precision while letting maps reach +-32 m. */
#define WORLD_SCALE 4
#define M2V(f) ((v16)((f) * (4096.0f / WORLD_SCALE)))

static u16 shadeRGB(int r, int g, int b, int shade /*0..32*/)
{
    r = (r * shade) >> 5; g = (g * shade) >> 5; b = (b * shade) >> 5;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return RGB15(r >> 3, g >> 3, b >> 3);
}

static RQuad *addQuad(Vec3 a, Vec3 b, Vec3 c, Vec3 d)
{
    if (numRQuads >= MAX_RQUADS) return NULL;
    RQuad *q = &rquads[numRQuads++];
    Vec3 p[4] = { a, b, c, d };
    for (int i = 0; i < 4; i++) {
        q->v[i][0] = M2V(p[i].x);
        q->v[i][1] = M2V(p[i].y);
        q->v[i][2] = M2V(p[i].z);
    }
    q->gate = -1;
    q->shade = 32;
    return q;
}

static void quadColor(RQuad *q, u16 c0, u16 c1, u16 c2, u16 c3)
{
    if (!q) return;
    q->col[0] = c0; q->col[1] = c1; q->col[2] = c2; q->col[3] = c3;
}

static void bakeBox(const Box *bx)
{
    const MapInfo *m = world;
    float x0 = bx->mn.x, y0 = bx->mn.y, z0 = bx->mn.z;
    float x1 = bx->mx.x, y1 = bx->mx.y, z1 = bx->mx.z;
    /* face order: top, bottom, -X, +X, -Z, +Z with fake directional light */
    static const u8 shades[6] = { 34, 18, 24, 28, 22, 30 };
    Vec3 f[6][4] = {
        { {x0,y1,z0}, {x0,y1,z1}, {x1,y1,z1}, {x1,y1,z0} },
        { {x0,y0,z0}, {x1,y0,z0}, {x1,y0,z1}, {x0,y0,z1} },
        { {x0,y0,z0}, {x0,y0,z1}, {x0,y1,z1}, {x0,y1,z0} },
        { {x1,y0,z0}, {x1,y1,z0}, {x1,y1,z1}, {x1,y0,z1} },
        { {x0,y0,z0}, {x0,y1,z0}, {x1,y1,z0}, {x1,y0,z0} },
        { {x0,y0,z1}, {x1,y0,z1}, {x1,y1,z1}, {x0,y1,z1} },
    };
    for (int i = 0; i < 6; i++) {
        /* hidden faces: on the floor, on the ceiling, flush with a wall */
        if (i == 1 && y0 < 0.001f) continue;
        if (i == 0 && m->indoor && y1 > m->ceilH - 0.001f) continue;
        if (i == 2 && x0 < m->minX + 0.001f) continue;
        if (i == 3 && x1 > m->maxX - 0.001f) continue;
        if (i == 4 && z0 < m->minZ + 0.001f) continue;
        if (i == 5 && z1 > m->maxZ - 0.001f) continue;
        RQuad *q = addQuad(f[i][0], f[i][1], f[i][2], f[i][3]);
        if (!q) return;
        u16 c = shadeRGB(bx->r, bx->g, bx->b, shades[i]);
        quadColor(q, c, c, c, c);
        q->gate = bx->gate;
        q->shade = shades[i];
    }
}

/* ---- floor and walls ----------------------------------------------------
 * Every surface is cut into tiles, and each tile is further split along the
 * edges of the patches (rug, door, window...) that overlap it. Each piece
 * gets the patch colour on top, or the surface colour, and then the same
 * per-vertex lighting as the rest of the surface. Nothing is layered on top
 * of anything else, so there is no z-fighting and no lighting mismatch. */

typedef struct { int r, g, b; } RGB;

static Vec3 surfPoint(Surface s, float u, float v)
{
    const MapInfo *m = world;
    switch (s) {
    case SURF_BACK:  return v3(u, v, m->minZ);
    case SURF_FRONT: return v3(u, v, m->maxZ);
    case SURF_LEFT:  return v3(m->minX, v, u);
    case SURF_RIGHT: return v3(m->maxX, v, u);
    default:         return v3(u, 0.0f, v);
    }
}

/* vertex brightness: floors are flat-lit, walls darken toward the bottom */
static int surfShade(Surface s, float v)
{
    if (s == SURF_FLOOR) return 32;
    const MapInfo *m = world;
    int lo = m->indoor ? 18 : 15, hi = m->indoor ? 31 : 28;
    return lo + (int)((hi - lo) * v / m->wallH);
}

static int addEdges(float *e, int n, float lo, float hi, float a, float b)
{
    if (a > lo + 0.001f && a < hi - 0.001f) e[n++] = a;
    if (b > lo + 0.001f && b < hi - 0.001f) e[n++] = b;
    return n;
}

static void sortEdges(float *e, int n)
{
    for (int i = 1; i < n; i++) {
        float t = e[i];
        int j = i - 1;
        while (j >= 0 && e[j] > t) { e[j + 1] = e[j]; j--; }
        e[j + 1] = t;
    }
}

static void bakeTile(Surface s, float u0, float v0, float u1, float v1, RGB base)
{
    float us[2 + 2 * MAX_PATCHES], vs[2 + 2 * MAX_PATCHES];
    int nu = 0, nv = 0;
    us[nu++] = u0; us[nu++] = u1;
    vs[nv++] = v0; vs[nv++] = v1;
    for (int i = 0; i < worldNumPatches; i++) {
        const Patch *p = &worldPatches[i];
        if (p->surf != s || p->u1 <= u0 || p->u0 >= u1 || p->v1 <= v0 || p->v0 >= v1)
            continue;
        nu = addEdges(us, nu, u0, u1, p->u0, p->u1);
        nv = addEdges(vs, nv, v0, v1, p->v0, p->v1);
    }
    sortEdges(us, nu);
    sortEdges(vs, nv);

    for (int j = 0; j + 1 < nv; j++) {
        if (vs[j + 1] - vs[j] < 0.001f) continue;
        for (int i = 0; i + 1 < nu; i++) {
            if (us[i + 1] - us[i] < 0.001f) continue;
            float uc = (us[i] + us[i + 1]) * 0.5f, vc = (vs[j] + vs[j + 1]) * 0.5f;
            RGB c = base;
            for (int k = 0; k < worldNumPatches; k++) {
                const Patch *p = &worldPatches[k];
                if (p->surf == s && uc > p->u0 && uc < p->u1 && vc > p->v0 && vc < p->v1) {
                    c.r = p->r; c.g = p->g; c.b = p->b;
                }
            }
            float cu[4] = { us[i], us[i], us[i + 1], us[i + 1] };
            float cv[4] = { vs[j], vs[j + 1], vs[j + 1], vs[j] };
            RQuad *q = addQuad(surfPoint(s, cu[0], cv[0]), surfPoint(s, cu[1], cv[1]),
                               surfPoint(s, cu[2], cv[2]), surfPoint(s, cu[3], cv[3]));
            if (!q) return;
            u16 col[4];
            for (int k = 0; k < 4; k++)
                col[k] = shadeRGB(c.r, c.g, c.b, surfShade(s, cv[k]));
            quadColor(q, col[0], col[1], col[2], col[3]);
        }
    }
}

static void bakeRoom(void)
{
    const MapInfo *m = world;
    const bool in = m->indoor;

    /* floor: checkered wood indoors, mown grass stripes outdoors */
    const float t = in ? 0.5f : 2.0f;
    const RGB fa = in ? (RGB){ 176, 120, 72 } : (RGB){ 74, 146, 62 };
    const RGB fb = in ? (RGB){ 152, 96, 56 }  : (RGB){ 62, 128, 52 };
    int ix = 0;
    for (float x = m->minX; x < m->maxX - 0.01f; x += t, ix++) {
        int iz = 0;
        for (float z = m->minZ; z < m->maxZ - 0.01f; z += t, iz++)
            bakeTile(SURF_FLOOR, x, z, x + t, z + t, ((ix + iz) & 1) ? fa : fb);
    }

    /* walls (a hedge outdoors), split into vertical strips so long walls clip nicely */
    const RGB wallCol[4] = {
        in ? (RGB){ 236, 222, 180 } : (RGB){ 48, 110, 46 },   /* back  */
        in ? (RGB){ 236, 222, 180 } : (RGB){ 48, 110, 46 },   /* front */
        in ? (RGB){ 180, 205, 220 } : (RGB){ 44, 102, 42 },   /* left  */
        in ? (RGB){ 180, 205, 220 } : (RGB){ 44, 102, 42 },   /* right */
    };
    for (int w = 0; w < 4; w++) {
        Surface s = (Surface)(SURF_BACK + w);
        float a = w < 2 ? m->minX : m->minZ, b = w < 2 ? m->maxX : m->maxZ;
        const int strips = in ? 4 : 8;
        for (int k = 0; k < strips; k++)
            bakeTile(s, a + (b - a) * k / strips, 0.0f, a + (b - a) * (k + 1) / strips,
                     m->wallH, wallCol[w]);
    }

    if (in) {
        /* ceiling, brighter near the lamp */
        u16 cEdge = RGB15(22, 22, 21), cMid = RGB15(30, 30, 28);
        float xs[3] = { m->minX, 0.0f, m->maxX };
        float zs[3] = { m->minZ, 0.0f, m->maxZ };
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 2; j++) {
                RQuad *q = addQuad(v3(xs[i], m->ceilH, zs[j]), v3(xs[i + 1], m->ceilH, zs[j]),
                                   v3(xs[i + 1], m->ceilH, zs[j + 1]), v3(xs[i], m->ceilH, zs[j + 1]));
                /* the centre vertex of the ceiling (above the lamp) is the bright one */
                u16 c[4];
                Vec3 vv[4] = { {xs[i],0,zs[j]}, {xs[i+1],0,zs[j]}, {xs[i+1],0,zs[j+1]}, {xs[i],0,zs[j+1]} };
                for (int k = 0; k < 4; k++)
                    c[k] = (vv[k].x == 0.0f && vv[k].z == 0.0f) ? cMid : cEdge;
                quadColor(q, c[0], c[1], c[2], c[3]);
            }
        return;
    }

    /* outdoors: give the hedge a top and an outer face, and lay a field
     * around it so there is ground to see when flying above the hedge */
    const float th = 0.6f, H = m->wallH, F = 30.0f;
    float x0 = m->minX, x1 = m->maxX, z0 = m->minZ, z1 = m->maxZ;
    u16 top = shadeRGB(48, 110, 46, 34), side = shadeRGB(48, 110, 46, 22);
    u16 sideLo = shadeRGB(48, 110, 46, 14);
    /* tops */
    quadColor(addQuad(v3(x0 - th, H, z0 - th), v3(x1 + th, H, z0 - th), v3(x1 + th, H, z0), v3(x0 - th, H, z0)), top, top, top, top);
    quadColor(addQuad(v3(x0 - th, H, z1), v3(x1 + th, H, z1), v3(x1 + th, H, z1 + th), v3(x0 - th, H, z1 + th)), top, top, top, top);
    quadColor(addQuad(v3(x0 - th, H, z0), v3(x0, H, z0), v3(x0, H, z1), v3(x0 - th, H, z1)), top, top, top, top);
    quadColor(addQuad(v3(x1, H, z0), v3(x1 + th, H, z0), v3(x1 + th, H, z1), v3(x1, H, z1)), top, top, top, top);
    /* outer faces */
    quadColor(addQuad(v3(x0 - th, 0, z0 - th), v3(x0 - th, H, z0 - th), v3(x1 + th, H, z0 - th), v3(x1 + th, 0, z0 - th)), sideLo, side, side, sideLo);
    quadColor(addQuad(v3(x0 - th, 0, z1 + th), v3(x0 - th, H, z1 + th), v3(x1 + th, H, z1 + th), v3(x1 + th, 0, z1 + th)), sideLo, side, side, sideLo);
    quadColor(addQuad(v3(x0 - th, 0, z0 - th), v3(x0 - th, H, z0 - th), v3(x0 - th, H, z1 + th), v3(x0 - th, 0, z1 + th)), sideLo, side, side, sideLo);
    quadColor(addQuad(v3(x1 + th, 0, z0 - th), v3(x1 + th, H, z0 - th), v3(x1 + th, H, z1 + th), v3(x1 + th, 0, z1 + th)), sideLo, side, side, sideLo);
    /* surrounding fields */
    u16 fc = RGB15(9, 16, 6), fn = RGB15(13, 20, 9);
    float ox0 = x0 - th, ox1 = x1 + th, oz0 = z0 - th, oz1 = z1 + th;
    quadColor(addQuad(v3(-F, 0, -F), v3(F, 0, -F), v3(F, 0, oz0), v3(-F, 0, oz0)), fc, fc, fn, fn);
    quadColor(addQuad(v3(-F, 0, oz1), v3(F, 0, oz1), v3(F, 0, F), v3(-F, 0, F)), fn, fn, fc, fc);
    quadColor(addQuad(v3(-F, 0, oz0), v3(ox0, 0, oz0), v3(ox0, 0, oz1), v3(-F, 0, oz1)), fc, fn, fn, fc);
    quadColor(addQuad(v3(ox1, 0, oz0), v3(F, 0, oz0), v3(F, 0, oz1), v3(ox1, 0, oz1)), fn, fc, fc, fn);
}

/* ------------------------------------------------------------------------ */
/* 3x5 pixel font for the OSD, drawn as 3D quads in an ortho projection.    */
/* ------------------------------------------------------------------------ */

#define G(a, b, c, d, e) (((a) << 12) | ((b) << 9) | ((c) << 6) | ((d) << 3) | (e))
static u16 glyph(char ch)
{
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    switch (ch) {
    case '0': return G(07, 05, 05, 05, 07);
    case '1': return G(02, 06, 02, 02, 07);
    case '2': return G(07, 01, 07, 04, 07);
    case '3': return G(07, 01, 07, 01, 07);
    case '4': return G(05, 05, 07, 01, 01);
    case '5': return G(07, 04, 07, 01, 07);
    case '6': return G(07, 04, 07, 05, 07);
    case '7': return G(07, 01, 01, 02, 02);
    case '8': return G(07, 05, 07, 05, 07);
    case '9': return G(07, 05, 07, 01, 07);
    case 'A': return G(02, 05, 07, 05, 05);
    case 'B': return G(06, 05, 06, 05, 06);
    case 'C': return G(03, 04, 04, 04, 03);
    case 'D': return G(06, 05, 05, 05, 06);
    case 'E': return G(07, 04, 06, 04, 07);
    case 'F': return G(07, 04, 06, 04, 04);
    case 'G': return G(03, 04, 05, 05, 03);
    case 'H': return G(05, 05, 07, 05, 05);
    case 'I': return G(07, 02, 02, 02, 07);
    case 'J': return G(01, 01, 01, 05, 02);
    case 'K': return G(05, 05, 06, 05, 05);
    case 'L': return G(04, 04, 04, 04, 07);
    case 'M': return G(05, 07, 07, 05, 05);
    case 'N': return G(06, 05, 05, 05, 05);
    case 'O': return G(02, 05, 05, 05, 02);
    case 'P': return G(06, 05, 06, 04, 04);
    case 'Q': return G(02, 05, 05, 06, 03);
    case 'R': return G(06, 05, 06, 05, 05);
    case 'S': return G(03, 04, 02, 01, 06);
    case 'T': return G(07, 02, 02, 02, 02);
    case 'U': return G(05, 05, 05, 05, 07);
    case 'V': return G(05, 05, 05, 05, 02);
    case 'W': return G(05, 05, 07, 07, 05);
    case 'X': return G(05, 05, 02, 05, 05);
    case 'Y': return G(05, 05, 02, 02, 02);
    case 'Z': return G(07, 01, 02, 04, 07);
    case ':': return G(00, 02, 00, 02, 00);
    case '.': return G(00, 00, 00, 00, 02);
    case '-': return G(00, 00, 07, 00, 00);
    case '%': return G(05, 01, 02, 04, 05);
    case '/': return G(01, 01, 02, 04, 04);
    case '!': return G(02, 02, 02, 00, 02);
    case '<': return G(01, 02, 04, 02, 01);
    case '>': return G(04, 02, 01, 02, 04);
    case ',': return G(00, 00, 00, 02, 04);
    default:  return 0;
    }
}
#undef G

/* OSD coordinates are screen pixels; 1 ortho unit = 64 px so v16 can hold them. */
#define PX(p) ((v16)((p) << 6))
#define OSD_Z      ((v16)(0.95f * 4096))
#define OSD_Z_BACK ((v16)(0.90f * 4096))

static void osdRect(int x, int y, int w, int h, v16 z)
{
    glVertex3v16(PX(x), PX(y), z);
    glVertex3v16(PX(x), PX(y + h), z);
    glVertex3v16(PX(x + w), PX(y + h), z);
    glVertex3v16(PX(x + w), PX(y), z);
}

static int textWidth(const char *s, int scale)
{
    return (int)strlen(s) * 4 * scale - scale;
}

/* Draws text with a 1px dark drop shadow. Horizontal runs of lit pixels are
 * merged into one quad each to keep the polygon count low. */
static void osdText(int x, int y, int scale, u16 color, const char *s)
{
    for (int pass = 0; pass < 2; pass++) {
        int ox = pass == 0 ? 1 : 0;
        glColor(pass == 0 ? RGB15(2, 2, 3) : color);
        v16 z = pass == 0 ? OSD_Z_BACK : OSD_Z;
        int cx = x;
        for (const char *p = s; *p; p++, cx += 4 * scale) {
            u16 gl = glyph(*p);
            for (int row = 0; row < 5; row++) {
                int bits = (gl >> (12 - row * 3)) & 7;
                int col = 0;
                while (col < 3) {
                    if (bits & (4 >> col)) {
                        int start = col;
                        while (col < 3 && (bits & (4 >> col))) col++;
                        osdRect(cx + start * scale + ox, y + row * scale + ox,
                                (col - start) * scale, scale, z);
                    } else {
                        col++;
                    }
                }
            }
        }
    }
}

static void osdTextCentered(int y, int scale, u16 color, const char *s)
{
    osdText((256 - textWidth(s, scale)) / 2, y, scale, color, s);
}

/* Centre-screen message. Lines are split on '\n'; the biggest scale (3..1)
 * at which every line fits between the throttle bar and the right edge is
 * used, so long messages shrink instead of running off the screen. */
#define MSG_MAX_W   (256 - 2 * 18)
#define MSG_MAX_LN  3
static void osdMessage(int y, u16 color, const char *msg)
{
    char lines[MSG_MAX_LN][32];
    int n = 0, len = 0;
    for (const char *p = msg; ; p++) {
        if (*p == '\n' || *p == 0) {
            lines[n][len] = 0;
            if (++n == MSG_MAX_LN || *p == 0) break;
            len = 0;
        } else if (len < 31) {
            lines[n][len++] = *p;
        }
    }
    int scale = 3;
    for (; scale > 1; scale--) {
        int ok = 1;
        for (int i = 0; i < n; i++)
            if (textWidth(lines[i], scale) > MSG_MAX_W) ok = 0;
        if (ok) break;
    }
    for (int i = 0; i < n; i++)
        osdTextCentered(y + i * 7 * scale, scale, color, lines[i]);
}

static void fmtTime(char *buf, int frames)
{
    if (frames < 0) { strcpy(buf, "-:--.-"); return; }
    int tenths = frames / 6;
    sprintf(buf, "%d:%02d.%d", tenths / 600, (tenths / 10) % 60, tenths % 10);
}

static void drawOSD(const Game *g)
{
    const Quad *q = &g->quad;
    char buf[32];

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrthof32(0, inttof32(4), inttof32(3), 0, inttof32(-1), inttof32(1));
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glBegin(GL_QUADS);

    /* centre crosshair */
    glColor(RGB15(31, 31, 31));
    osdRect(122, 95, 4, 2, OSD_Z);
    osdRect(130, 95, 4, 2, OSD_Z);
    osdRect(127, 90, 2, 4, OSD_Z);
    osdRect(127, 98, 2, 4, OSD_Z);

    /* flight mode / arm state */
    osdText(6, 6, 2, RGB15(31, 31, 31), q->mode == MODE_ACRO ? "ACRO" : "ANGLE");
    if (q->armed) {
        osdText(256 - 6 - textWidth("ARMED", 2), 6, 2, RGB15(31, 8, 6), "ARMED");
    } else {
        osdText(256 - 6 - textWidth("DISARMED", 2), 6, 2, RGB15(31, 31, 31), "DISARMED");
    }

    /* lap timer */
    fmtTime(buf, g->lapRunning ? g->lapFrames : -1);
    osdTextCentered(6, 2, RGB15(31, 31, 10), buf);

    /* throttle + speed */
    sprintf(buf, "THR %d%%", (int)(g->sticks.throttle * 100.0f + 0.5f));
    osdText(6, 192 - 6 - 10, 2, RGB15(31, 31, 31), buf);
    int kmh10 = (int)(vlen(q->vel) * 36.0f);
    sprintf(buf, "%d.%d KMH", kmh10 / 10, kmh10 % 10);
    osdText(256 - 6 - textWidth(buf, 2), 192 - 6 - 10, 2, RGB15(31, 31, 31), buf);

    /* throttle bar on the left edge */
    int barH = (int)(g->sticks.throttle * 100.0f);
    glColor(RGB15(4, 4, 6));
    osdRect(6, 46, 6, 102, OSD_Z_BACK);
    glColor(RGB15(10, 31, 12));
    osdRect(7, 47 + (100 - barH), 4, barH, OSD_Z);

    /* gate counter */
    sprintf(buf, "GATE %d/%d", g->nextGate + 1, worldNumGates);
    osdTextCentered(192 - 6 - 10, 2, RGB15(10, 31, 12), buf);

    if (g->msg)
        osdMessage(56, RGB15(31, 24, 4), g->msg);

    glEnd();
}

/* ------------------------------------------------------------------------ */

void renderInit(void)
{
    videoSetMode(MODE_0_3D);
    lcdMainOnTop();

    glInit();
    glEnable(GL_ANTIALIAS);
    glClearPolyID(63);
    glClearDepth(0x7FFF);
    glViewport(0, 0, 255, 191);
}

void renderBake(void)
{
    if (world->indoor) glClearColor(4, 4, 6, 31);
    else               glClearColor(15, 22, 31, 31);   /* sky */

    numRQuads = 0;
    bakeRoom();
    for (int i = 0; i < worldNumBoxes; i++)
        bakeBox(&worldBoxes[i]);
}

static void loadProjection(void)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(78, 256.0f / 192.0f, 0.03f, world->indoor ? 12.0f : 45.0f);
}

static void loadCamera(const Quad *q)
{
    const float tilt = CAM_UPTILT_DEG * (3.14159265f / 180.0f);
    float c = cosf(tilt), s = sinf(tilt);
    Vec3 r = q->rot.r;
    Vec3 u = vadd(vscale(q->rot.u, c), vscale(q->rot.b, s));
    Vec3 b = vsub(vscale(q->rot.b, c), vscale(q->rot.u, s));
    Vec3 p = q->pos;

    m4x4 m;
    m.m[0] = floattof32(r.x); m.m[4] = floattof32(r.y); m.m[8]  = floattof32(r.z); m.m[12] = floattof32(-vdot(r, p));
    m.m[1] = floattof32(u.x); m.m[5] = floattof32(u.y); m.m[9]  = floattof32(u.z); m.m[13] = floattof32(-vdot(u, p));
    m.m[2] = floattof32(b.x); m.m[6] = floattof32(b.y); m.m[10] = floattof32(b.z); m.m[14] = floattof32(-vdot(b, p));
    m.m[3] = 0; m.m[7] = 0; m.m[11] = 0; m.m[15] = inttof32(1);

    glMatrixMode(GL_MODELVIEW);
    glLoadMatrix4x4(&m);
}

static u16 gateColor(int nextGate, int gate, int shade)
{
    if (gate == nextGate)  return shadeRGB(60, 255, 90, shade);    /* next: green  */
    if (gate == (nextGate + 1) % worldNumGates)
                           return shadeRGB(255, 200, 40, shade);   /* after: yellow */
    return shadeRGB(255, 90, 30, shade);                           /* others       */
}

/* Draws the baked scene with the camera already in the modelview matrix. */
static void drawScene(int nextGate)
{
    glScalef32(inttof32(WORLD_SCALE), inttof32(WORLD_SCALE), inttof32(WORLD_SCALE));
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(1));

    glBegin(GL_QUADS);
    for (int i = 0; i < numRQuads; i++) {
        const RQuad *q = &rquads[i];
        if (q->gate >= 0) {
            glColor(gateColor(nextGate, q->gate, q->shade));
            for (int k = 0; k < 4; k++)
                glVertex3v16(q->v[k][0], q->v[k][1], q->v[k][2]);
        } else {
            for (int k = 0; k < 4; k++) {
                glColor(q->col[k]);
                glVertex3v16(q->v[k][0], q->v[k][1], q->v[k][2]);
            }
        }
    }
    glEnd();
}

void renderFrame(const Game *g)
{
    loadProjection();
    loadCamera(&g->quad);
    drawScene(g->nextGate);

    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(2));
    drawOSD(g);

    glFlush(0);
}

/* Map select: slow orbit around the selected map, with the menu on top. */
void renderMenu(const Game *g, int frame)
{
    const MapInfo *m = world;
    float a = frame * 0.004f;
    float rad = m->indoor ? 1.9f : 10.0f, h = m->indoor ? 1.9f : 6.0f;
    float cx = (m->minX + m->maxX) * 0.5f, cz = (m->minZ + m->maxZ) * 0.5f;

    loadProjection();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(cx + rad * cosf(a), h, cz + rad * sinf(a), cx, m->indoor ? 0.6f : 1.0f, cz, 0.0f, 1.0f, 0.0f);
    drawScene(0);

    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(2));
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrthof32(0, inttof32(4), inttof32(3), 0, inttof32(-1), inttof32(1));
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glBegin(GL_QUADS);
    osdTextCentered(8, 3, RGB15(31, 31, 31), "WHOOPDSI");
    osdTextCentered(32, 2, RGB15(31, 31, 10), "SELECT MAP");
    for (int i = 0; i < MAP_COUNT; i++) {
        char buf[24];
        bool sel = i == g->menuSel;
        sprintf(buf, sel ? "> %s <" : "%s", mapInfo[i].name);
        osdTextCentered(70 + i * 30, sel ? 3 : 2, sel ? RGB15(10, 31, 12) : RGB15(24, 24, 24), buf);
    }
    osdTextCentered(140, 1, RGB15(31, 31, 31), mapInfo[g->menuSel].desc);
    if ((frame / 30) & 1)
        osdTextCentered(170, 2, RGB15(31, 24, 4), "PRESS A TO FLY");
    glEnd();

    glFlush(0);
}
