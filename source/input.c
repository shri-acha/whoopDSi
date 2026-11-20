#include <nds.h>
#include "input.h"

/* How fast a centering axis travels toward full deflection while a button is
 * held (units per second), and how fast it springs back when released.
 * Tapping a button gives a small correction, holding it gives full stick. */
#define RAMP_UP     3.2f
#define RAMP_DOWN   7.0f
#define EXPO        0.25f

/* Throttle moves while Up/Down is held and stays where it is left.
 * It speeds up the longer the button is held for coarse + fine control. */
#define THR_RATE_MIN 0.30f
#define THR_RATE_MAX 0.90f

static float thrHoldTime;

static float ramp(float v, int target, float dt)
{
    if (target != 0) {
        /* reversing direction: snap through centre quickly */
        if (v * target < 0.0f) {
            v += target * RAMP_DOWN * dt;
            if (v * target > 0.0f) v = 0.0f;
            return v;
        }
        v += target * RAMP_UP * dt;
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
    } else {
        if (v > 0.0f) { v -= RAMP_DOWN * dt; if (v < 0.0f) v = 0.0f; }
        else if (v < 0.0f) { v += RAMP_DOWN * dt; if (v > 0.0f) v = 0.0f; }
    }
    return v;
}

static float expo(float v)
{
    return v * (1.0f - EXPO) + v * v * v * EXPO;
}

void inputInit(Sticks *s)
{
    s->throttle = 0.0f;
    s->yaw = s->pitch = s->roll = 0.0f;
    s->rawYaw = s->rawPitch = s->rawRoll = 0.0f;
    s->armToggle = s->modeToggle = s->reset = s->throttleCut = s->speedToggle = false;
    thrHoldTime = 0.0f;
}

void inputUpdate(Sticks *s, float dt)
{
    scanKeys();
    u32 held = keysHeld();
    u32 down = keysDown();

    /* --- left stick: throttle (sticky) + yaw --- */
    int thrDir = ((held & KEY_UP) ? 1 : 0) - ((held & KEY_DOWN) ? 1 : 0);
    if (thrDir != 0) {
        thrHoldTime += dt;
        float t = thrHoldTime / 0.6f;
        if (t > 1.0f) t = 1.0f;
        float rate = THR_RATE_MIN + (THR_RATE_MAX - THR_RATE_MIN) * t;
        s->throttle += thrDir * rate * dt;
        if (s->throttle < 0.0f) s->throttle = 0.0f;
        if (s->throttle > 1.0f) s->throttle = 1.0f;
    } else {
        thrHoldTime = 0.0f;
    }

    int yawDir = ((held & KEY_RIGHT) ? 1 : 0) - ((held & KEY_LEFT) ? 1 : 0);
    s->rawYaw = ramp(s->rawYaw, yawDir, dt);

    /* --- right stick: pitch (X fwd / B back) + roll (A right / Y left) --- */
    int pitchDir = ((held & KEY_X) ? 1 : 0) - ((held & KEY_B) ? 1 : 0);
    int rollDir  = ((held & KEY_A) ? 1 : 0) - ((held & KEY_Y) ? 1 : 0);
    s->rawPitch = ramp(s->rawPitch, pitchDir, dt);
    s->rawRoll  = ramp(s->rawRoll, rollDir, dt);

    s->yaw   = expo(s->rawYaw);
    s->pitch = expo(s->rawPitch);
    s->roll  = expo(s->rawRoll);

    s->armToggle   = (down & KEY_START) != 0;
    s->modeToggle  = (down & KEY_L) != 0;
    s->reset       = (down & KEY_SELECT) != 0;
    s->throttleCut = (down & KEY_R) != 0;
    s->speedToggle = (down & KEY_TOUCH) != 0;
    if (s->throttleCut) s->throttle = 0.0f;
}
