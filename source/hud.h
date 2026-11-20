#ifndef HUD_H
#define HUD_H

#include "game.h"

/* Bottom screen: flight info text + live stick monitor. */
void hudInit(void);
void hudUpdate(const Game *g, int frame);
/* Map index under a touch at bottom-screen pixel row py, or -1. */
int  hudMenuHit(int py);

#endif
