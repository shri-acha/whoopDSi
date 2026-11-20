#ifndef RENDER_H
#define RENDER_H

#include "game.h"

/* Top screen: 3D FPV view + OSD drawn with the DS 3D engine. */
void renderInit(void);
void renderFrame(const Game *g);

#endif
