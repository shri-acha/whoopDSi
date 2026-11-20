/*
 * whoopDSi - a tiny whoop FPV simulator for Nintendo DS / DSi homebrew.
 *
 * Top screen:    FPV camera view rendered with the DS 3D engine + OSD.
 * Bottom screen: flight info and a live "radio" stick monitor.
 */
#include <nds.h>
#include "game.h"
#include "input.h"
#include "quad.h"
#include "world.h"
#include "render.h"
#include "hud.h"

#define DT          (1.0f / 60.0f)
#define SUBSTEPS    2
#define CRASH_SPEED 3.2f   /* m/s into a surface while armed = crash */
#define ARM_MAX_THR 0.05f  /* like Betaflight: throttle must be low to arm */

static Game game;

/* Boot-stage indicator: paints both screens' backdrop a solid colour.
 * Normally each colour is only visible for a frame or two; if the program
 * ever hangs during start-up, the colour tells you where:
 *   red     = main() reached
 *   yellow  = starting the 3D engine, then building and baking the map
 *   cyan    = map baked, setting up the bottom screen HUD
 * A completely white screen means main() was never reached (launcher issue). */
static void bootStage(u16 color)
{
    BG_PALETTE[0] = color;
    BG_PALETTE_SUB[0] = color;
}

static void showMsg(const char *m, int frames)
{
    game.msg = m;
    game.msgTimer = frames;
}

static void resetRun(bool keepMode)
{
    FlightMode mode = game.quad.mode;
    quadReset(&game.quad, worldStart);
    game.quad.mode = keepMode ? mode : MODE_ANGLE;
    game.sticks.throttle = 0.0f;
    game.nextGate = 0;
    game.lapRunning = false;
    game.lapFrames = 0;
    showMsg("PRESS START\nTO ARM", -1);
}

static void gateLogic(Vec3 prev, Vec3 cur)
{
    if (!worldGateCrossed(game.nextGate, prev, cur)) return;

    if (game.nextGate == 0) {
        if (game.lapRunning) {
            game.lastFrames = game.lapFrames;
            if (game.bestFrames < 0 || game.lapFrames < game.bestFrames) {
                game.bestFrames = game.lapFrames;
                showMsg("BEST LAP!", 90);
            } else {
                showMsg("LAP", 60);
            }
            game.laps++;
        } else {
            showMsg("GO!", 45);
        }
        game.lapRunning = true;
        game.lapFrames = 0;
    }
    game.nextGate = (game.nextGate + 1) % worldNumGates;
}

/* Builds a map and bakes it for the renderer. Lap records are per map. */
static void loadMap(int map)
{
    if (map == game.map) return;
    game.map = map;
    worldInit((MapId)map);
    renderBake();
    game.lastFrames = -1;
    game.bestFrames = -1;
    game.laps = 0;
}

static void openMenu(void)
{
    game.quad.armed = false;
    game.state = STATE_MENU;
    game.menuSel = game.map;
}

/* Map select screen: Up/Down or a tap picks the map, A or START flies it. */
static void updateMenu(void)
{
    u32 down = keysDown();
    int sel = game.menuSel;

    if (down & KEY_UP)   sel = (sel + MAP_COUNT - 1) % MAP_COUNT;
    if (down & KEY_DOWN) sel = (sel + 1) % MAP_COUNT;
    bool fly = (down & (KEY_A | KEY_START)) != 0;
    if (down & KEY_TOUCH) {
        touchPosition t;
        touchRead(&t);
        int hit = hudMenuHit(t.py);
        if (hit >= 0) {
            fly = hit == sel;   /* tap once to pick, tap again to fly */
            sel = hit;
        }
    }

    if (sel != game.menuSel) {
        game.menuSel = sel;
        loadMap(sel);          /* rebake so the preview shows it */
    }
    if (fly) {
        game.state = STATE_FLY;
        resetRun(true);
    }
}

static void update(void)
{
    Sticks *s = &game.sticks;
    Quad *q = &game.quad;

    inputUpdate(s, DT);

    if (game.state == STATE_MENU) {
        updateMenu();
        return;
    }
    if (s->menu) {
        openMenu();
        return;
    }
    if (s->reset) {
        resetRun(true);
        return;
    }
    if (s->modeToggle) {
        q->mode = q->mode == MODE_ANGLE ? MODE_ACRO : MODE_ANGLE;
        showMsg(q->mode == MODE_ACRO ? "ACRO MODE" : "ANGLE MODE", 60);
    }
    if (s->speedToggle) {
        q->speed = (q->speed + 1) % SPEED_COUNT;
        static const char *const names[SPEED_COUNT] = { "SLOW SPEED", "MEDIUM SPEED", "FAST SPEED" };
        showMsg(names[q->speed], 60);
    }
    if (s->armToggle) {
        if (q->armed) {
            q->armed = false;
            showMsg("DISARMED", 60);
        } else if (q->crashed) {
            showMsg("CRASHED\nSELECT TO RESET", -1);
        } else if (s->throttle > ARM_MAX_THR) {
            showMsg("LOWER THROTTLE\nTO ARM", 90);
        } else {
            q->armed = true;
            showMsg("ARMED", 45);
        }
    }

    Vec3 prev = q->pos;
    for (int i = 0; i < SUBSTEPS; i++) {
        quadStep(q, s, DT / SUBSTEPS);
        float impact = worldCollide(q);
        if (q->armed && impact > CRASH_SPEED) {
            q->armed = false;
            q->crashed = true;
            showMsg("CRASHED\nSELECT TO RESET", -1);
        }
    }
    gateLogic(prev, q->pos);

    if (game.lapRunning) game.lapFrames++;

    if (game.msgTimer > 0 && --game.msgTimer == 0)
        game.msg = NULL;
}

int main(void)
{
    videoSetMode(MODE_0_2D);
    videoSetModeSub(MODE_0_2D);
    bootStage(RGB15(31, 0, 0));
    game.map = -1;
    bootStage(RGB15(31, 31, 0));
    renderInit();
    loadMap(MAP_INDOOR);
    bootStage(RGB15(0, 31, 31));
    hudInit();
    BG_PALETTE[0] = RGB15(0, 0, 0);

    inputInit(&game.sticks);
    game.quad.mode = MODE_ANGLE;
    game.quad.speed = SPEED_SLOW;
    resetRun(false);
    openMenu();

    int frame = 0;
    while (pmMainLoop()) {
        update();
        if (game.state == STATE_MENU) renderMenu(&game, frame);
        else                          renderFrame(&game);
        hudUpdate(&game, frame++);
        swiWaitForVBlank();
    }
    return 0;
}
