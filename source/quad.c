#include "quad.h"

#define DEG2RAD (3.14159265f / 180.0f)

/* ---- airframe (65 mm brushless whoop-ish) ---- */
#define GRAVITY       9.81f
#define THRUST_WEIGHT 2.6f    /* thrust-to-weight at full throttle */
#define IDLE_THROTTLE 0.05f   /* motors spin at idle when armed (air mode-ish) */
#define DRAG_LIN      0.55f   /* 1/s           */
#define DRAG_QUAD     0.35f   /* 1/m           */
#define RATE_TAU      0.045f  /* how quickly the frame reaches the commanded rate (s) */

/* ---- acro rates, Betaflight style ---- */
#define RC_RATE       1.0f
#define SUPER_RATE    0.62f
#define YAW_RC_RATE   0.9f
#define YAW_SUPER     0.45f

/* ---- angle mode ---- */
#define ANGLE_MAX_DEG 50.0f
#define ANGLE_P       9.0f    /* (rad/s) per rad of tilt error */
#define ANGLE_YAW_DPS 220.0f

/* ---- speed profiles ----
 * Lower thrust-to-weight makes the throttle less twitchy (hover moves up
 * from ~36% to ~50%), extra drag caps the top speed, and scaled rates and a
 * smaller max lean angle make the quad turn and tilt more gently. */
typedef struct {
    const char *name;
    float thrustWeight;
    float dragLin;
    float rateScale;    /* acro + angle-mode yaw rates */
    float angleMaxDeg;
} SpeedParams;

static const SpeedParams speeds[SPEED_COUNT] = {
    /* name     T/W   drag  rates  angle */
    { "SLOW",   1.9f, 1.60f, 0.50f, 25.0f },
    { "MEDIUM", 2.2f, 1.00f, 0.75f, 35.0f },
    { "FAST",   THRUST_WEIGHT, DRAG_LIN, 1.00f, ANGLE_MAX_DEG },
};

const char *quadSpeedName(SpeedProfile sp)
{
    return speeds[sp].name;
}

static float bfRate(float x, float rc, float sup)
{
    float ax = x < 0 ? -x : x;
    float r = 200.0f * rc * x;
    float sf = 1.0f / (1.0f - ax * sup);
    return r * sf * DEG2RAD; /* rad/s */
}

static void orthonormalize(Mat3 *m)
{
    m->u = vnorm(m->u);
    m->r = vnorm(vcross(m->u, m->b));
    m->b = vcross(m->r, m->u);
}

void quadReset(Quad *q, Vec3 startPos)
{
    q->pos = startPos;
    q->vel = v3(0, 0, 0);
    q->rot.r = v3(1, 0, 0);
    q->rot.u = v3(0, 1, 0);
    q->rot.b = v3(0, 0, 1);
    q->rate = v3(0, 0, 0);
    q->armed = false;
    q->crashed = false;
    q->thrustOut = 0.0f;
}

static Vec3 angleModeRates(const Quad *q, const Sticks *s)
{
    const SpeedParams *sp = &speeds[q->speed];
    Vec3 up = q->rot.u;
    Vec3 worldUp = v3(0, 1, 0);

    /* heading: body forward projected on the horizontal plane */
    Vec3 fwd = vscale(q->rot.b, -1.0f);
    fwd.y = 0.0f;
    if (vlen(fwd) < 0.05f) {
        /* nose pointing straight up/down: use the up vector's lean instead */
        fwd = q->rot.u;
        fwd.y = 0.0f;
        if (vlen(fwd) < 0.05f) fwd = v3(0, 0, -1);
    }
    fwd = vnorm(fwd);
    Vec3 right = vcross(fwd, worldUp);

    float pitchAng = s->pitch * sp->angleMaxDeg * DEG2RAD;
    float rollAng  = s->roll * sp->angleMaxDeg * DEG2RAD;
    Vec3 upDesired = vnorm(vadd(worldUp,
                       vadd(vscale(fwd, tanf(pitchAng)), vscale(right, tanf(rollAng)))));

    /* axis that rotates the current up toward the desired up (world) */
    Vec3 err = vcross(up, upDesired);
    /* if fully inverted the cross product vanishes: kick it over on roll */
    if (vdot(up, upDesired) < 0.0f && vlen(err) < 0.2f)
        err = vadd(err, vscale(fwd, 1.0f));

    Vec3 eb = mtmul(&q->rot, err);
    Vec3 cmd;
    cmd.x = ANGLE_P * eb.x;
    cmd.z = ANGLE_P * eb.z;
    cmd.y = -s->yaw * ANGLE_YAW_DPS * sp->rateScale * DEG2RAD;
    return cmd;
}

void quadStep(Quad *q, const Sticks *s, float dt)
{
    const SpeedParams *sp = &speeds[q->speed];

    /* --- commanded body rates --- */
    Vec3 cmd;
    if (q->armed) {
        if (q->mode == MODE_ACRO) {
            /* + pitch stick = nose down = negative rotation about +X, etc. */
            cmd.x = -bfRate(s->pitch, RC_RATE, SUPER_RATE) * sp->rateScale;
            cmd.y = -bfRate(s->yaw, YAW_RC_RATE, YAW_SUPER) * sp->rateScale;
            cmd.z = -bfRate(s->roll, RC_RATE, SUPER_RATE) * sp->rateScale;
        } else {
            cmd = angleModeRates(q, s);
        }
        q->thrustOut = IDLE_THROTTLE + (1.0f - IDLE_THROTTLE) * s->throttle;
    } else {
        /* motors off: whatever rotation there is slowly dies out from drag */
        cmd = vscale(q->rate, 0.97f);
        q->thrustOut = 0.0f;
    }

    float k = dt / RATE_TAU;
    if (k > 1.0f) k = 1.0f;
    q->rate = vadd(q->rate, vscale(vsub(cmd, q->rate), k));

    /* --- integrate orientation: R' = R * (I + [w]x dt) (body-frame rates) --- */
    Vec3 w = vscale(q->rate, dt);
    Mat3 n;
    /* columns of R * skew: r' = r + (wz * u - wy * b), etc. */
    n.r = vadd(q->rot.r, vsub(vscale(q->rot.u, w.z), vscale(q->rot.b, w.y)));
    n.u = vadd(q->rot.u, vsub(vscale(q->rot.b, w.x), vscale(q->rot.r, w.z)));
    n.b = vadd(q->rot.b, vsub(vscale(q->rot.r, w.y), vscale(q->rot.u, w.x)));
    q->rot = n;
    orthonormalize(&q->rot);

    /* --- translational dynamics --- */
    Vec3 acc = vscale(q->rot.u, q->thrustOut * sp->thrustWeight * GRAVITY);
    acc.y -= GRAVITY;
    float speed = vlen(q->vel);
    acc = vsub(acc, vscale(q->vel, sp->dragLin + DRAG_QUAD * speed));

    q->vel = vadd(q->vel, vscale(acc, dt));
    q->pos = vadd(q->pos, vscale(q->vel, dt));

    /* disarmed and lying on the floor: settle flat on the ducts */
    if (!q->armed && q->pos.y <= QUAD_RADIUS + 0.005f && q->rot.u.y > 0.0f) {
        q->rot.u = vnorm(vadd(q->rot.u, vscale(vsub(v3(0, 1, 0), q->rot.u), 8.0f * dt)));
        orthonormalize(&q->rot);
    }
}
