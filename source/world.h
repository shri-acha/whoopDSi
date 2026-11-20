#ifndef WORLD_H
#define WORLD_H

#include <stdbool.h>
#include "vmath.h"
#include "quad.h"

/* Maps. Y is up, every map is centred on X/Z, units are metres. */
typedef enum { MAP_INDOOR = 0, MAP_OUTDOOR = 1, MAP_COUNT } MapId;

typedef struct {
    const char *name;   /* menu name                                        */
    const char *desc;   /* one-line description for the menu                */
    float minX, maxX, minZ, maxZ;
    float wallH;        /* height of the drawn boundary walls               */
    float ceilH;        /* flight ceiling (the real ceiling indoors)        */
    bool  indoor;       /* indoor: solid ceiling and walls all the way up.
                         * outdoor: walls are a hedge, above it a soft net  */
} MapInfo;

#define MAX_BOXES   128
#define MAX_PATCHES 48
#define MAX_GATES   8

typedef struct {
    Vec3 mn, mx;
    unsigned char r, g, b;
    bool collide;
    signed char gate;   /* index of the gate this bar belongs to, or -1 */
} Box;

/* Flat coloured rectangle painted onto the floor or a boundary wall (rug,
 * door, window...). Patches are baked into the floor/wall mesh itself, so
 * they get the same lighting as the surface and can never z-fight with it. */
typedef enum { SURF_FLOOR, SURF_BACK, SURF_FRONT, SURF_LEFT, SURF_RIGHT } Surface;

typedef struct {
    Surface surf;
    /* floor: (u,v) = (x,z). back/front walls: (x,y). left/right walls: (z,y) */
    float u0, v0, u1, v1;
    unsigned char r, g, b;
} Patch;

typedef struct {
    Vec3 c;        /* centre of the opening              */
    int axis;      /* 0: gate plane faces X, 2: faces Z  */
    int dir;       /* +1 / -1: direction it must be flown through */
    float half;    /* half size of the square opening    */
} Gate;

extern const MapInfo mapInfo[MAP_COUNT];
extern MapId  worldMap;
extern const MapInfo *world;   /* == &mapInfo[worldMap] */

extern Box   worldBoxes[MAX_BOXES];
extern int   worldNumBoxes;
extern Patch worldPatches[MAX_PATCHES];
extern int   worldNumPatches;
extern Gate  worldGates[MAX_GATES];
extern int   worldNumGates;
extern Vec3  worldStart;

/* Builds the given map. Call renderBake() afterwards. */
void worldInit(MapId map);

/* Pushes the quad out of walls/furniture. Returns the largest impact speed
 * (m/s, velocity into the surface) of this step, 0 if no contact. */
float worldCollide(Quad *q);

/* Returns true if the segment a->b flies through gate g in its direction. */
bool worldGateCrossed(int g, Vec3 a, Vec3 b);

#endif
