/*
 * fsm_host_hw.h
 * Nahrada TIM6, tlacidla a LED pre test fsm.c na PC.
 */
#ifndef FSM_HOST_HW_H
#define FSM_HOST_HW_H

#include <stdbool.h>

extern bool mock_tim_flag;
extern bool mock_tim_started;
extern bool mock_btn_down;
extern bool mock_led;

static inline void hw_tick_start(void) { mock_tim_flag = false; mock_tim_started = true; }
static inline bool hw_tick_flag(void) { return mock_tim_flag; }
static inline void hw_tick_clear(void) { mock_tim_flag = false; }
static inline bool hw_button_down(void) { return mock_btn_down; }
static inline void hw_led(bool on) { mock_led = on; }

#endif /* FSM_HOST_HW_H */