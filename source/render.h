#ifndef RENDER_H
#define RENDER_H

#include "game.h"

/* Top screen: 3D FPV view + OSD drawn with the DS 3D engine. */
void renderInit(void);
/* Bakes the current world (call after worldInit). */
void renderBake(void);
void renderFrame(const Game *g);
/* Map select screen: orbiting preview of the selected map + menu. */
void renderMenu(const Game *g, int frame);

#endif
