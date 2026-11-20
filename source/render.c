#include <nds.h>
#include <stdio.h>
#include <string.h>
#include "render.h"
#include "world.h"

/* ------------------------------------------------------------------------ */
/* Static scene, baked once into fixed-point quads with per-vertex colour.  */
/* ------------------------------------------------------------------------ */

#define MAX_RQUADS 900

typedef struct {
    v16 v[4][3];
    u16 col[4];
    s8  gate;    /* >=0: colour comes from gate state at draw time */
    u8  shade;   /* 0..32 face brightness, used for gate colouring */
} RQuad;

static RQuad rquads[MAX_RQUADS];
static int numRQuads;

#define M2V(f) ((v16)((f) * 4096.0f))   /* metres -> v16 (4.12 fixed) */

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
        if (i == 1 && y0 < 0.001f) continue;              /* resting on the floor */
        if (i == 0 && y1 > ROOM_H - 0.001f) continue;     /* touching the ceiling */
        if (i == 2 && x0 < ROOM_MIN_X + 0.001f) continue; /* flush with a wall    */
        if (i == 3 && x1 > ROOM_MAX_X - 0.001f) continue;
        if (i == 4 && z0 < ROOM_MIN_Z + 0.001f) continue;
        if (i == 5 && z1 > ROOM_MAX_Z - 0.001f) continue;
        RQuad *q = addQuad(f[i][0], f[i][1], f[i][2], f[i][3]);
        if (!q) return;
        u16 c = shadeRGB(bx->r, bx->g, bx->b, shades[i]);
        quadColor(q, c, c, c, c);
        q->gate = bx->gate;
        q->shade = shades[i];
    }
}

static void bakeRoom(void)
{
    /* checkered wooden floor, 0.5 m tiles */
    const float t = 0.5f;
    int ix = 0;
    for (float x = ROOM_MIN_X; x < ROOM_MAX_X - 0.01f; x += t, ix++) {
        int iz = 0;
        for (float z = ROOM_MIN_Z; z < ROOM_MAX_Z - 0.01f; z += t, iz++) {
            RQuad *q = addQuad(v3(x, 0, z), v3(x, 0, z + t), v3(x + t, 0, z + t), v3(x + t, 0, z));
            u16 c = ((ix + iz) & 1) ? RGB15(22, 15, 9) : RGB15(19, 12, 7);
            quadColor(q, c, c, c, c);
        }
    }

    /* walls: darker at the bottom for a bit of ambient occlusion feel,
     * split into vertical strips so long walls clip nicely */
    struct { float x0, z0, x1, z1; int r, g, b; } walls[4] = {
        { ROOM_MIN_X, ROOM_MIN_Z, ROOM_MAX_X, ROOM_MIN_Z, 236, 222, 180 }, /* back  */
        { ROOM_MAX_X, ROOM_MAX_Z, ROOM_MIN_X, ROOM_MAX_Z, 236, 222, 180 }, /* front */
        { ROOM_MIN_X, ROOM_MAX_Z, ROOM_MIN_X, ROOM_MIN_Z, 180, 205, 220 }, /* left  */
        { ROOM_MAX_X, ROOM_MIN_Z, ROOM_MAX_X, ROOM_MAX_Z, 180, 205, 220 }, /* right */
    };
    for (int w = 0; w < 4; w++) {
        const int strips = 4;
        u16 lo = shadeRGB(walls[w].r, walls[w].g, walls[w].b, 18);
        u16 hi = shadeRGB(walls[w].r, walls[w].g, walls[w].b, 31);
        for (int s = 0; s < strips; s++) {
            float a = (float)s / strips, b = (float)(s + 1) / strips;
            float xa = walls[w].x0 + (walls[w].x1 - walls[w].x0) * a;
            float za = walls[w].z0 + (walls[w].z1 - walls[w].z0) * a;
            float xb = walls[w].x0 + (walls[w].x1 - walls[w].x0) * b;
            float zb = walls[w].z0 + (walls[w].z1 - walls[w].z0) * b;
            RQuad *q = addQuad(v3(xa, 0, za), v3(xa, ROOM_H, za), v3(xb, ROOM_H, zb), v3(xb, 0, zb));
            quadColor(q, lo, hi, hi, lo);
        }
        /* skirting board */
        float bx0 = walls[w].x0, bz0 = walls[w].z0, bx1 = walls[w].x1, bz1 = walls[w].z1;
        RQuad *q = addQuad(v3(bx0, 0, bz0), v3(bx0, 0.08f, bz0), v3(bx1, 0.08f, bz1), v3(bx1, 0, bz1));
        u16 sk = RGB15(29, 29, 29);
        quadColor(q, sk, sk, sk, sk);
    }

    /* ceiling, brighter near the lamp */
    u16 cEdge = RGB15(22, 22, 21), cMid = RGB15(30, 30, 28);
    float xs[3] = { ROOM_MIN_X, 0.0f, ROOM_MAX_X };
    float zs[3] = { ROOM_MIN_Z, 0.0f, ROOM_MAX_Z };
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) {
            RQuad *q = addQuad(v3(xs[i], ROOM_H, zs[j]), v3(xs[i + 1], ROOM_H, zs[j]),
                               v3(xs[i + 1], ROOM_H, zs[j + 1]), v3(xs[i], ROOM_H, zs[j + 1]));
            /* the centre vertex of the ceiling (above the lamp) is the bright one */
            u16 c[4];
            Vec3 vv[4] = { {xs[i],0,zs[j]}, {xs[i+1],0,zs[j]}, {xs[i+1],0,zs[j+1]}, {xs[i],0,zs[j+1]} };
            for (int k = 0; k < 4; k++)
                c[k] = (vv[k].x == 0.0f && vv[k].z == 0.0f) ? cMid : cEdge;
            quadColor(q, c[0], c[1], c[2], c[3]);
        }
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
    glClearColor(4, 4, 6, 31);
    glClearPolyID(63);
    glClearDepth(0x7FFF);
    glViewport(0, 0, 255, 191);
    BG_PALETTE_SUB[0] = RGB15(31, 0, 31);   /* boot stage: 3D up, baking */

    numRQuads = 0;
    bakeRoom();
    for (int i = 0; i < worldNumBoxes; i++)
        bakeBox(&worldBoxes[i]);
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

static u16 gateColor(const Game *g, int gate, int shade)
{
    if (gate == g->nextGate)  return shadeRGB(60, 255, 90, shade);    /* next: green  */
    if (gate == (g->nextGate + 1) % worldNumGates)
                              return shadeRGB(255, 200, 40, shade);   /* after: yellow */
    return shadeRGB(255, 90, 30, shade);                              /* others       */
}

void renderFrame(const Game *g)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(78, 256.0f / 192.0f, 0.03f, 12.0f);

    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(1));
    loadCamera(&g->quad);

    glBegin(GL_QUADS);
    for (int i = 0; i < numRQuads; i++) {
        const RQuad *q = &rquads[i];
        if (q->gate >= 0) {
            glColor(gateColor(g, q->gate, q->shade));
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

    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(2));
    drawOSD(g);

    glFlush(0);
}
