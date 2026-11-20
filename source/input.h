#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>

/* Virtual "radio" sticks, Mode 2.
 *   throttle: 0..1   (non-centering, D-pad Up/Down)
 *   yaw:     -1..1   (D-pad Left/Right, + = right)
 *   pitch:   -1..1   (X / B, + = forward / nose down)
 *   roll:    -1..1   (Y / A, + = right)
 */
typedef struct {
    float throttle, yaw, pitch, roll;
    /* raw ramped values before expo, used for the stick monitor */
    float rawYaw, rawPitch, rawRoll;

    /* one-shot actions this frame */
    bool armToggle;   /* START  */
    bool modeToggle;  /* L      */
    bool reset;       /* SELECT */
    bool throttleCut; /* R      */
    bool speedToggle; /* touch screen tap */
    bool menu;        /* SELECT held for a second: back to map select */
} Sticks;

void inputInit(Sticks *s);
void inputUpdate(Sticks *s, float dt);

#endif
