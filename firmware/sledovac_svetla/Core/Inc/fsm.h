/*
 * fsm.h
 * Stavový automat režimov TRACK / AVOID / SEARCH / MANUAL / CALIB
 * Zodpovedá: člen 4
 */
#ifndef FSM_H
#define FSM_H

#include <stdint.h>
#include <stdbool.h>

void fsm_init(void);

#endif /* FSM_H */
