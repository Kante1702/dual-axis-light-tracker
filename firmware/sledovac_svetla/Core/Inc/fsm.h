/*
 * fsm.h
 * Stavový automat režimov TRACK / AVOID / SEARCH / MANUAL / CALIB
 * Zodpovedá: člen 4 (Kanta)
 */
#ifndef FSM_H
#define FSM_H

#include <stdint.h>
#include <stdbool.h>

/* Číselné hodnoty sa posielajú v telemetrii (stĺpec mode),
 * preto sa poradie nesmie meniť. Nové režimy iba pred MODE_COUNT. */
typedef enum {
    MODE_TRACK  = 0,   /* sledovanie zdroja             */
    MODE_AVOID  = 1,   /* odvracanie od zdroja          */
    MODE_SEARCH = 2,   /* vyhľadávanie strateného zdroja*/
    MODE_MANUAL = 3,   /* ručné ovládanie (GOTO)        */
    MODE_CALIB  = 4,   /* kalibrácia snímačov           */
    MODE_COUNT
} fsm_mode_t;

void fsm_init(void);

/* Jeden krok automatu, volá sa každých 20 ms z hlavnej slučky. */
void fsm_tick(void);

fsm_mode_t fsm_get_mode(void);

/* Požiadavka na zmenu režimu (UART príkaz MODE).
 * Zmena sa vykoná na začiatku najbližšieho ticku.
 * Vráti false pri neplatnom režime. */
bool fsm_request_mode(fsm_mode_t m);

/* Príkaz GOTO: prepne do MANUAL a nastaví cieľovú polohu. */
bool fsm_manual_goto(float pan_deg, float tilt_deg);

/* Volá EXTI prerušenie tlačidla, iba nastaví príznak.
 * Prepnutie režimu (s ošetrením zákmitov) spraví fsm_tick(). */
void fsm_on_button(void);

#endif /* FSM_H */
