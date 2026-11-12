/*
 * control.h
 * Regulácia: krokový, P a PI regulátor pre obe osi, slučka 20 ms (TIM6)
 * Zodpovedá: člen 3
 */
#ifndef CONTROL_H
#define CONTROL_H

#include <stdint.h>
#include <stdbool.h>

void control_init(void);

#endif /* CONTROL_H */
