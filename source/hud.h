#ifndef HUD_H
#define HUD_H

#include "game.h"

/* Bottom screen: flight info text + live stick monitor. */
void hudInit(void);
void hudUpdate(const Game *g, int frame);

#endif
