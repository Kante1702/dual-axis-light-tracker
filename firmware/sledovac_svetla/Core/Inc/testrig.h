#ifndef TESTRIG_H
#define TESTRIG_H

#include <stdint.h>
#include <stdbool.h>

#define RIG_T_S        0.020f
#define RIG_LAMP_MIN   20.0f
#define RIG_LAMP_MAX   160.0f
#define RIG_CENTER     90.0f

typedef enum {
    RIG_PROF_JUMP = 0,
    RIG_PROF_RAMP,
    RIG_PROF_SINE
} rig_profile_type_t;

typedef struct {
    rig_profile_type_t type;
    float    start_deg;
    float    end_deg;
    float    speed_dps;
    float    amp_deg;
    float    period_s;
    uint16_t cycles;
    float    hold_s;
} rig_profile_t;

typedef enum {
    RIG_IDLE = 0,
    RIG_GOTO_START,
    RIG_SETTLE,
    RIG_RUN,
    RIG_DONE
} rig_phase_t;

void        testrig_init(void);
bool        testrig_start(const rig_profile_t *p);
bool        testrig_stop(void);
void        testrig_step(void);

bool        testrig_jump(float target_deg);
bool        testrig_ramp(float speed_dps);
bool        testrig_sine(float amp_deg, float period_s);

bool        testrig_running(void);
rig_phase_t testrig_phase(void);
float       testrig_angle(void);
float       testrig_run_time(void);

#endif
