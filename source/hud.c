#include <nds.h>
#include <stdio.h>
#include "hud.h"
#include "world.h"

/* Sub engine layout (VRAM C, 128 KB):
 *   BG0  text console : map @ 0 KB, font tiles @ 16 KB
 *   BG2  8bpp bitmap  : @ 64 KB (256x256), used for the stick monitor
 * Bitmap palette indices are picked so they don't collide with the
 * console's colour entries (n*16+15). */
#define C_PANEL  129
#define C_BORDER 130
#define C_CROSS  131
#define C_DOT    132
#define C_TRAIL  133

#define BOX_SIZE 64
#define BOX_Y    68
#define LBOX_X   28
#define RBOX_X   164

static PrintConsole con;
static u16 *bmp;

static inline void px(int x, int y, u8 c)
{
    u16 *p = &bmp[(y * 256 + x) >> 1];
    if (x & 1) *p = (*p & 0x00FF) | (c << 8);
    else       *p = (*p & 0xFF00) | c;
}

static void fillRect(int x, int y, int w, int h, u8 c)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            px(i, j, c);
}

static void drawStickBox(int bx, int by, float sx, float sy /* -1..1, +up */)
{
    fillRect(bx, by, BOX_SIZE, BOX_SIZE, C_PANEL);
    /* border */
    for (int i = 0; i < BOX_SIZE; i++) {
        px(bx + i, by, C_BORDER);
        px(bx + i, by + BOX_SIZE - 1, C_BORDER);
        px(bx, by + i, C_BORDER);
        px(bx + BOX_SIZE - 1, by + i, C_BORDER);
    }
    /* centre cross */
    for (int i = 4; i < BOX_SIZE - 4; i++) {
        px(bx + BOX_SIZE / 2, by + i, C_CROSS);
        px(bx + i, by + BOX_SIZE / 2, C_CROSS);
    }
    /* stick gimbal line + dot */
    int cx = bx + BOX_SIZE / 2, cy = by + BOX_SIZE / 2;
    int dx = (int)(sx * (BOX_SIZE / 2 - 6));
    int dy = (int)(-sy * (BOX_SIZE / 2 - 6));
    int steps = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    for (int s = 0; s <= steps; s++) {
        int x = cx + (steps ? dx * s / steps : 0);
        int y = cy + (steps ? dy * s / steps : 0);
        px(x, y, C_TRAIL);
    }
    fillRect(cx + dx - 3, cy + dy - 3, 7, 7, C_DOT);
}

void hudInit(void)
{
    videoSetModeSub(MODE_5_2D);
    vramSetBankC(VRAM_C_SUB_BG);

    consoleInit(&con, 0, BgType_Text4bpp, BgSize_T_256x256, 0, 1, false, true);

    int bg = bgInitSub(2, BgType_Bmp8, BgSize_B8_256x256, 4, 0);
    bgSetPriority(bg, 1);
    bmp = bgGetGfxPtr(bg);
    for (int i = 0; i < 256 * 192 / 2; i++) bmp[i] = 0;

    BG_PALETTE_SUB[0]        = RGB15(2, 3, 6);    /* backdrop */
    BG_PALETTE_SUB[C_PANEL]  = RGB15(5, 6, 10);
    BG_PALETTE_SUB[C_BORDER] = RGB15(16, 18, 24);
    BG_PALETTE_SUB[C_CROSS]  = RGB15(9, 10, 15);
    BG_PALETTE_SUB[C_DOT]    = RGB15(10, 31, 12);
    BG_PALETTE_SUB[C_TRAIL]  = RGB15(6, 20, 8);

}

/* Map menu rows on the bottom screen: map i is drawn on text rows
 * MENU_ROW0 + i * MENU_STEP (name) and the row below it (description). */
#define MENU_ROW0 6
#define MENU_STEP 4

int hudMenuHit(int py)
{
    int row = py / 8;
    for (int i = 0; i < MAP_COUNT; i++) {
        int r = MENU_ROW0 + i * MENU_STEP;
        if (row >= r - 1 && row <= r + 2) return i;
    }
    return -1;
}

static void drawStatic(GameState state)
{
    consoleSelect(&con);
    consoleClear();
    for (int i = 0; i < 256 * 192 / 2; i++) bmp[i] = 0;
    iprintf("\x1b[0;0H  whoopDSi - tiny whoop FPV sim");
    if (state == STATE_MENU) {
        iprintf("\x1b[3;1HSELECT MAP");
        iprintf("\x1b[18;1HUP/DOWN or tap   choose map");
        iprintf("\x1b[19;1HA / START or tap again  fly");
        iprintf("\x1b[21;1HIn flight, hold SELECT for");
        iprintf("\x1b[22;1Hone second to come back here.");
        return;
    }
    iprintf("\x1b[17;3HTHR / YAW");
    iprintf("\x1b[17;20HPITCH / ROLL");
    iprintf("\x1b[19;0H UP/DOWN throttle LEFT/RIGHT yaw");
    iprintf("\x1b[20;0H X/B pitch        Y/A roll");
    iprintf("\x1b[21;0H START arm        L angle/acro");
    iprintf("\x1b[22;0H R throttle cut   SELECT reset");
    iprintf("\x1b[23;0H hold SELECT      map select");
}

static void drawMenu(const Game *g)
{
    consoleSelect(&con);
    for (int i = 0; i < MAP_COUNT; i++) {
        int r = MENU_ROW0 + i * MENU_STEP;
        bool sel = i == g->menuSel;
        iprintf("\x1b[%d;1H%s %-10s", r, sel ? ">>" : "  ", mapInfo[i].name);
        iprintf("\x1b[%d;4H%-26s", r + 1, mapInfo[i].desc);
    }
}

static void fmtTime(char *buf, int frames)
{
    if (frames < 0) { sprintf(buf, "-:--.-"); return; }
    int tenths = frames / 6;
    sprintf(buf, "%d:%02d.%d", tenths / 600, (tenths / 10) % 60, tenths % 10);
}

void hudUpdate(const Game *g, int frame)
{
    const Quad *q = &g->quad;
    const Sticks *s = &g->sticks;

    static int shown = -1;
    if ((int)g->state != shown) {
        shown = g->state;
        drawStatic(g->state);
    }
    if (g->state == STATE_MENU) {
        drawMenu(g);
        return;
    }

    /* stick monitor every frame (cheap) */
    drawStickBox(LBOX_X, BOX_Y, s->rawYaw, s->throttle * 2.0f - 1.0f);
    drawStickBox(RBOX_X, BOX_Y, s->rawRoll, s->rawPitch);

    /* text a few times a second is plenty */
    if (frame % 4) return;

    char lap[16], last[16], best[16];
    fmtTime(lap, g->lapRunning ? g->lapFrames : -1);
    fmtTime(last, g->lastFrames);
    fmtTime(best, g->bestFrames);
    int kmh10 = (int)(vlen(q->vel) * 36.0f);
    int altcm = (int)(q->pos.y * 100.0f);

    consoleSelect(&con);
    iprintf("\x1b[1;1HMAP  %-8s", world->name);
    iprintf("\x1b[2;1HMODE %-6s %-10s      ",
            q->mode == MODE_ACRO ? "ACRO" : "ANGLE",
            q->crashed ? "CRASHED" : (q->armed ? "ARMED" : "DISARMED"));
    iprintf("\x1b[3;1HLAP  %-8s LAPS %-4d      ", lap, g->laps);
    iprintf("\x1b[4;1HLAST %-8s BEST %-8s  ", last, best);
    iprintf("\x1b[5;1HGATE %d/%d      SPD %2d.%d km/h  ", g->nextGate + 1, worldNumGates,
            kmh10 / 10, kmh10 % 10);
    iprintf("\x1b[7;1HSPEED %-6s  (tap screen)    ", quadSpeedName(q->speed));
    iprintf("\x1b[6;1HALT  %d.%02d m   THR %3d%%      ", altcm / 100, altcm % 100,
            (int)(s->throttle * 100.0f + 0.5f));
}
