/*
 * comm_hooks.h
 * Funkcie inych modulov, ktore vola parser prikazov v comm.c.
 * Kym ich modul neimplementuje, pouzije sa weak verzia z comm_hooks.c
 * a prikaz odpovie #ERR NOT_IMPL.
 * Kto funkciu implementuje, nech v svojom .c includne comm_hooks.h,
 * kompilator potom skontroluje parametre.
 */


#ifndef COMM_HOOKS_H
#define COMM_HOOKS_H

#include <stdint.h>
#include <stdbool.h>
#include "fsm.h"

#define AXIS_PAN   0u
#define AXIS_TILT  1u


/* control.c */
bool control_set_type(uint8_t type);   /* 0 STEP, 1 P, 2 PI */
bool control_set_kp(float kp);
bool control_set_ki(float ki);
bool control_set_step(float step_deg);
bool control_set_deadband(float db);
bool control_set_sign(uint8_t axis, int8_t sign);   /* axis 0 pan, 1 tilt; sign 1 / -1 */

/* motion.c */
bool motion_set_vmax(float vmax_deg_s);
bool motion_set_limits(uint8_t axis, float min_deg, float max_deg);

/* calib.c */
bool calib_capture_dark(void);
bool calib_capture_bright(void);
bool calib_save(void);

/* testrig.c */
bool testrig_ramp(float speed_deg_s);
bool testrig_sine(float amp_deg, float period_s);
bool testrig_jump(float angle_deg);
bool testrig_stop(void);

/* ina219.c */
bool ina219_reset_energy(void);

/* vola weak verzia, comm.c potom odpovie #ERR NOT_IMPL */
void comm_hook_missing(void);

#endif
/* COMM_HOOKS_H */
