#ifndef QUAD_H
#define QUAD_H

#include <stdbool.h>
#include "vmath.h"
#include "input.h"

#define QUAD_RADIUS   0.04f   /* collision sphere, ~65 mm whoop        */
#define CAM_UPTILT_DEG 15.0f  /* FPV camera tilt above the frame plane */

typedef enum { MODE_ANGLE = 0, MODE_ACRO = 1 } FlightMode;

/* Speed profiles: SLOW is a gentle trainer, FAST is the full-power whoop. */
typedef enum { SPEED_SLOW = 0, SPEED_MEDIUM = 1, SPEED_FAST = 2, SPEED_COUNT } SpeedProfile;

typedef struct {
    Vec3 pos, vel;
    Mat3 rot;        /* body -> world                     */
    Vec3 rate;       /* body rates rad/s (x=pitch, y=yaw, z=roll) */
    bool armed;
    bool crashed;
    FlightMode mode;
    SpeedProfile speed; /* kept across resets, like mode */
    float thrustOut; /* 0..1 actual motor output, for the HUD */
} Quad;

const char *quadSpeedName(SpeedProfile sp);
void quadReset(Quad *q, Vec3 startPos);
void quadStep(Quad *q, const Sticks *s, float dt);

#endif
