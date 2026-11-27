/*
 * comm_hooks.c
 * Docasne (weak) verzie funkcii z comm_hooks.h.
 * Ked modul napise vlastnu funkciu s rovnakym menom, pouzije sa ta jeho.
 */
#include "comm_hooks.h"

#define WEAK __attribute__((weak))

WEAK bool fsm_request_mode(fsm_mode_t m)
{
	(void)m;
	comm_hook_missing();
	return false;
}

WEAK bool fsm_manual_goto(float pan, float tilt)
{
	(void)pan;
	(void)tilt;
	comm_hook_missing();
	return false;
}

WEAK bool control_set_type(uint8_t type)
{
	(void)type;
	comm_hook_missing();
	return false;
}

WEAK bool control_set_kp(float kp)
{
	(void)kp;
	comm_hook_missing();
	return false;
}

WEAK bool control_set_ki(float ki)
{
	(void)ki;
	comm_hook_missing();
	return false;
}

WEAK bool control_set_step(float step_deg)
{
	(void)step_deg;
	comm_hook_missing();
	return false; }

WEAK bool control_set_deadband(float db)
{
	(void)db;
	comm_hook_missing();
	return false;
}

WEAK bool control_set_sign(uint8_t axis, int8_t sign)
{
	(void)axis;
	(void)sign;
	comm_hook_missing();
	return false;
}

WEAK bool motion_set_vmax(float vmax_deg_s)
{
	(void)vmax_deg_s;
	comm_hook_missing();
	return false;
}

WEAK bool motion_set_limits(uint8_t axis, float min_deg, float max_deg)
{
    (void)axis;
    (void)min_deg;
    (void)max_deg;
    comm_hook_missing();
    return false;
}

WEAK bool calib_capture_dark(void)
{
	comm_hook_missing();
	return false;
}

WEAK bool calib_capture_bright(void)
{
	comm_hook_missing();
	return false;
}

WEAK bool calib_save(void)
{
	comm_hook_missing();
	return false;
}

WEAK bool testrig_ramp(float speed_deg_s)
{
	(void)speed_deg_s;
	comm_hook_missing();
	return false;
}

WEAK bool testrig_sine(float amp_deg, float period_s)
{
	(void)amp_deg;
	(void)period_s;
	comm_hook_missing();
	return false;
}

WEAK bool testrig_jump(float angle_deg)
{
	(void)angle_deg;
	comm_hook_missing();
	return false;
}

WEAK bool testrig_stop(void)
{
	comm_hook_missing();
	return false;
}


WEAK bool sensors_set_lost_thresholds(float lost, float found)
{
	(void)lost;
	(void)found;
	comm_hook_missing();
	return false;
}

WEAK bool filter_set_alpha(float a)
{
	(void)a;
	comm_hook_missing();
	return false;
}

WEAK bool ina219_reset_energy(void)
{
	comm_hook_missing();
return false;
}
