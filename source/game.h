#ifndef GAME_H
#define GAME_H

#include <stdbool.h>
#include "quad.h"
#include "input.h"

typedef enum { STATE_MENU, STATE_FLY } GameState;

typedef struct {
    GameState state;
    int    menuSel;     /* map highlighted on the select screen          */
    int    map;         /* MapId of the map currently loaded             */

    Quad   quad;
    Sticks sticks;

    int  nextGate;      /* gate that has to be flown through next      */
    bool lapRunning;    /* timer started by passing gate 1              */
    int  lapFrames;     /* current lap time, 60 Hz frames               */
    int  lastFrames;    /* last completed lap, -1 if none               */
    int  bestFrames;    /* best lap, -1 if none                         */
    int  laps;

    const char *msg;    /* centre-screen OSD message, NULL for none     */
    int  msgTimer;      /* frames left to show it, <0 = sticky          */
} Game;

#endif
