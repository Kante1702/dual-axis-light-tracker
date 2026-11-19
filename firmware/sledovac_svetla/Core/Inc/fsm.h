/*
 * fsm.h
 * Stavovy automat rezimov
 */
#ifndef FSM_H
#define FSM_H

#include <stdint.h>
#include <stdbool.h>

/* hodnoty sa posielaju v telemetrii, poradie nemenit */
typedef enum {
    MODE_TRACK  = 0,
    MODE_AVOID  = 1,
    MODE_SEARCH = 2,
    MODE_MANUAL = 3,
    MODE_CALIB  = 4,
    MODE_COUNT
} fsm_mode_t;

void       fsm_init(void);
void       fsm_tick(void);      /* kazdych 20 ms */
fsm_mode_t fsm_get_mode(void);
bool       fsm_request_mode(fsm_mode_t m);
bool       fsm_manual_goto(float pan_deg, float tilt_deg);
void       fsm_on_button(void); /* volat z EXTI prerusenia */

#endif /* FSM_H */
