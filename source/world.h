#ifndef WORLD_H
#define WORLD_H

#include <stdbool.h>
#include "vmath.h"
#include "quad.h"

/* Indoor room, metres. Y is up, the room is centred on X/Z. */
#define ROOM_MIN_X -3.0f
#define ROOM_MAX_X  3.0f
#define ROOM_MIN_Z -2.5f
#define ROOM_MAX_Z  2.5f
#define ROOM_H      2.6f

#define MAX_BOXES 96
#define MAX_GATES 8

typedef struct {
    Vec3 mn, mx;
    unsigned char r, g, b;
    bool collide;
    signed char gate;   /* index of the gate this bar belongs to, or -1 */
} Box;

typedef struct {
    Vec3 c;        /* centre of the opening              */
    int axis;      /* 0: gate plane faces X, 2: faces Z  */
    int dir;       /* +1 / -1: direction it must be flown through */
    float half;    /* half size of the square opening    */
} Gate;

extern Box  worldBoxes[MAX_BOXES];
extern int  worldNumBoxes;
extern Gate worldGates[MAX_GATES];
extern int  worldNumGates;
extern Vec3 worldStart;

void worldInit(void);

/* Pushes the quad out of walls/furniture. Returns the largest impact speed
 * (m/s, velocity into the surface) of this step, 0 if no contact. */
float worldCollide(Quad *q);

/* Returns true if the segment a->b flies through gate g in its direction. */
bool worldGateCrossed(int g, Vec3 a, Vec3 b);

#endif
