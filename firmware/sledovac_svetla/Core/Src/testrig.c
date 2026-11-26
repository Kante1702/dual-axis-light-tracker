#include "testrig.h"
#include "comm_hooks.h"
#include <math.h>

#define RIG_GOTO_DPS      60.0f
#define RIG_SETTLE_S      2.0f
#define RIG_JUMP_HOLD_S   5.0f
#define RIG_RAMP_START    30.0f
#define RIG_RAMP_END      150.0f
#define RIG_SINE_CYCLES   3u
#define RIG_SPEED_MIN     1.0f
#define RIG_SPEED_MAX     90.0f
#define RIG_PERIOD_MIN    2.0f
#define RIG_PERIOD_MAX    60.0f
#define RIG_PI            3.14159265f

static rig_profile_t prof;
static rig_phase_t   phase;
static float         angle;
static float         duration;
static uint32_t      ticks;

static bool in_range(float a)
{
    return a >= RIG_LAMP_MIN && a <= RIG_LAMP_MAX;
}

static float profile_angle(float t)
{
    switch (prof.type) {
    case RIG_PROF_JUMP:
        return prof.end_deg;
    case RIG_PROF_RAMP: {
        float dir = (prof.end_deg >= prof.start_deg) ? 1.0f : -1.0f;
        return prof.start_deg + dir * prof.speed_dps * t;
    }
    case RIG_PROF_SINE:
        return prof.start_deg + prof.amp_deg * sinf(2.0f * RIG_PI * t / prof.period_s);
    }
    return angle;
}

static float profile_end(void)
{
    switch (prof.type) {
    case RIG_PROF_JUMP: return prof.end_deg;
    case RIG_PROF_RAMP: return prof.end_deg;
    case RIG_PROF_SINE: return prof.start_deg;
    }
    return angle;
}

void testrig_init(void)
{
    phase = RIG_IDLE;
    angle = RIG_CENTER;
    duration = 0.0f;
    ticks = 0;
}

bool testrig_start(const rig_profile_t *p)
{
    if (p == 0 || !in_range(p->start_deg))
        return false;

    switch (p->type) {
    case RIG_PROF_JUMP:
        if (!in_range(p->end_deg) || p->hold_s <= 0.0f)
            return false;
        duration = p->hold_s;
        break;
    case RIG_PROF_RAMP:
        if (!in_range(p->end_deg) || p->speed_dps < RIG_SPEED_MIN || p->speed_dps > RIG_SPEED_MAX)
            return false;
        duration = fabsf(p->end_deg - p->start_deg) / p->speed_dps;
        break;
    case RIG_PROF_SINE:
        if (!in_range(p->start_deg - p->amp_deg) || !in_range(p->start_deg + p->amp_deg) ||
            p->amp_deg <= 0.0f || p->period_s < RIG_PERIOD_MIN || p->period_s > RIG_PERIOD_MAX ||
            p->cycles == 0)
            return false;
        duration = p->period_s * (float)p->cycles;
        break;
    default:
        return false;
    }

    prof = *p;
    ticks = 0;
    phase = RIG_GOTO_START;
    return true;
}

bool testrig_stop(void)
{
    phase = RIG_IDLE;
    ticks = 0;
    return true;
}

void testrig_step(void)
{
    switch (phase) {
    case RIG_IDLE:
    case RIG_DONE:
        break;

    case RIG_GOTO_START: {
        float max_d = RIG_GOTO_DPS * RIG_T_S;
        float diff = prof.start_deg - angle;
        if (fabsf(diff) <= max_d) {
            angle = prof.start_deg;
            ticks = 0;
            phase = RIG_SETTLE;
        } else {
            angle += (diff > 0.0f) ? max_d : -max_d;
        }
        break;
    }

    case RIG_SETTLE:
        ticks++;
        if ((float)ticks * RIG_T_S >= RIG_SETTLE_S) {
            ticks = 0;
            phase = RIG_RUN;
        }
        break;

    case RIG_RUN: {
        float t = (float)ticks * RIG_T_S;
        if (t >= duration) {
            angle = profile_end();
            phase = RIG_DONE;
        } else {
            angle = profile_angle(t);
            ticks++;
        }
        break;
    }
    }
}

bool testrig_jump(float target_deg)
{
    rig_profile_t p = {0};
    p.type = RIG_PROF_JUMP;
    p.start_deg = RIG_CENTER;
    p.end_deg = target_deg;
    p.hold_s = RIG_JUMP_HOLD_S;
    return testrig_start(&p);
}

bool testrig_ramp(float speed_dps)
{
    rig_profile_t p = {0};
    p.type = RIG_PROF_RAMP;
    p.start_deg = RIG_RAMP_START;
    p.end_deg = RIG_RAMP_END;
    p.speed_dps = speed_dps;
    return testrig_start(&p);
}

bool testrig_sine(float amp_deg, float period_s)
{
    rig_profile_t p = {0};
    p.type = RIG_PROF_SINE;
    p.start_deg = RIG_CENTER;
    p.amp_deg = amp_deg;
    p.period_s = period_s;
    p.cycles = RIG_SINE_CYCLES;
    return testrig_start(&p);
}

bool testrig_running(void)
{
    return phase == RIG_GOTO_START || phase == RIG_SETTLE || phase == RIG_RUN;
}

rig_phase_t testrig_phase(void)
{
    return phase;
}

float testrig_angle(void)
{
    return angle;
}

float testrig_run_time(void)
{
    return (phase == RIG_RUN || phase == RIG_DONE) ? (float)ticks * RIG_T_S : 0.0f;
}
