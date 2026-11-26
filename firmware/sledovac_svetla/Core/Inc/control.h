#ifndef CONTROL_H
#define CONTROL_H

#include <stdint.h>
#include <stdbool.h>

#define CONTROL_T_S  0.020f

typedef enum {
    CTRL_STEP = 0,
    CTRL_P    = 1,
    CTRL_PI   = 2
} ctrl_type_t;

typedef enum {
    CTRL_AXIS_PAN = 0,
    CTRL_AXIS_TILT,
    CTRL_AXIS_COUNT
} ctrl_axis_t;

void        control_init(void);
void        control_reset(void);

bool        control_set_type(ctrl_type_t type);
ctrl_type_t control_get_type(void);
bool        control_set_kp(float kp);
bool        control_set_ki(float ki);
bool        control_set_step(float step_deg);
bool        control_set_deadband(float db);
bool        control_set_sign(ctrl_axis_t axis, int8_t sign);

void control_update(float e_h, float e_v, bool lost, uint32_t seq, bool avoid,
                    int8_t sat_pan, int8_t sat_tilt, float *d_pan, float *d_tilt);

#endif
