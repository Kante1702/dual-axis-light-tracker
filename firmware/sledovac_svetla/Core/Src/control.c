#include "control.h"
#include <math.h>

#define KP_MAX       20.0f
#define KI_MAX       5.0f
#define STEP_MAX     10.0f
#define DB_MAX       1.0f
#define SUM_LIMIT    500.0f
#define SAT_EPS      1e-4f

typedef struct {
    int8_t sign;
    float  sum;
    float  e_last;
    float  d_req;
    bool   integrated;
} axis_state_t;

static ctrl_type_t  type;
static float        kp;
static float        ki;
static float        step_deg;
static float        deadband;
static axis_state_t ax[CTRL_AXIS_COUNT];
static uint32_t     last_seq;
static bool         have_seq;

static float clampf(float x, float lo, float hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static float axis_step(axis_state_t *a, float e)
{
    float d = 0.0f;

    a->e_last = e;
    a->integrated = false;

    switch (type) {
    case CTRL_STEP:
        if (e > deadband)       d = step_deg;
        else if (e < -deadband) d = -step_deg;
        break;
    case CTRL_P:
        d = kp * e;
        break;
    case CTRL_PI:
        a->sum = clampf(a->sum + e, -SUM_LIMIT, SUM_LIMIT);
        a->integrated = true;
        d = kp * e + ki * a->sum;
        break;
    }

    a->d_req = d;
    return d;
}

static void axis_feedback(axis_state_t *a, float applied)
{
    if (!a->integrated)
        return;

    if (fabsf(applied) + SAT_EPS < fabsf(a->d_req) && a->d_req * a->e_last > 0.0f)
        a->sum -= a->e_last;

    a->integrated = false;
}

void control_init(void)
{
    type     = CTRL_P;
    kp       = 1.0f;
    ki       = 0.0f;
    step_deg = 1.0f;
    deadband = 0.02f;
    have_seq = false;
    last_seq = 0;
    for (int i = 0; i < CTRL_AXIS_COUNT; i++)
        ax[i].sign = 1;
    control_reset();
}

void control_reset(void)
{
    for (int i = 0; i < CTRL_AXIS_COUNT; i++) {
        ax[i].sum = 0.0f;
        ax[i].e_last = 0.0f;
        ax[i].d_req = 0.0f;
        ax[i].integrated = false;
    }
}

bool control_set_type(ctrl_type_t t)
{
    if (t != CTRL_STEP && t != CTRL_P && t != CTRL_PI)
        return false;
    type = t;
    control_reset();
    return true;
}

ctrl_type_t control_get_type(void)
{
    return type;
}

bool control_set_kp(float v)
{
    if (!(v >= 0.0f && v <= KP_MAX))
        return false;
    kp = v;
    return true;
}

bool control_set_ki(float v)
{
    if (!(v >= 0.0f && v <= KI_MAX))
        return false;
    ki = v;
    return true;
}

bool control_set_step(float v)
{
    if (!(v > 0.0f && v <= STEP_MAX))
        return false;
    step_deg = v;
    return true;
}

bool control_set_deadband(float v)
{
    if (!(v >= 0.0f && v < DB_MAX))
        return false;
    deadband = v;
    return true;
}

bool control_set_sign(ctrl_axis_t axis, int8_t sign)
{
    if (axis >= CTRL_AXIS_COUNT || (sign != 1 && sign != -1))
        return false;
    ax[axis].sign = sign;
    return true;
}

void control_update(float e_h, float e_v, bool lost, uint32_t seq, bool avoid,
                    float *d_pan, float *d_tilt)
{
    bool fresh = !have_seq || seq != last_seq;

    *d_pan = 0.0f;
    *d_tilt = 0.0f;

    if (fresh) {
        last_seq = seq;
        have_seq = true;
    }

    if (lost || !fresh) {
        for (int i = 0; i < CTRL_AXIS_COUNT; i++) {
            ax[i].d_req = 0.0f;
            ax[i].integrated = false;
        }
        return;
    }

    float k = avoid ? -1.0f : 1.0f;

    *d_pan  = axis_step(&ax[CTRL_AXIS_PAN],  k * ax[CTRL_AXIS_PAN].sign  * e_h);
    *d_tilt = axis_step(&ax[CTRL_AXIS_TILT], k * ax[CTRL_AXIS_TILT].sign * e_v);
}

void control_feedback(float applied_pan, float applied_tilt)
{
    axis_feedback(&ax[CTRL_AXIS_PAN],  applied_pan);
    axis_feedback(&ax[CTRL_AXIS_TILT], applied_tilt);
}
