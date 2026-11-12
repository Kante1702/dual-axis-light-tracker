/*
 * servo.h
 * Ovládač serv: PWM na TIM3, prevod uhol -> šírka impulzu, kalibrácia
 * Zodpovedá: člen 1
 */
#ifndef SERVO_H
#define SERVO_H

#include <stdint.h>
#include <stdbool.h>

void servo_init(void);

#endif /* SERVO_H */
