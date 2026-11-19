/*
 * comm.h
 * UART prikazy a telemetria, protokol je v docs/protokol_uart.md
 */
#ifndef COMM_H
#define COMM_H

#include <stdint.h>
#include <stdbool.h>

#define COMM_RX_BUF_SIZE   128u
#define COMM_TX_BUF_SIZE   512u
#define COMM_LINE_MAX       64u

/* jeden riadok telemetrie, hodnoty vo fyzikalnych jednotkach */
typedef struct {
    uint32_t t_ms;      /* cas od startu [ms] */
    float    ldr[4];    /* LH, PH, LD, PD, 0..1 */
    float    s;         /* sucet snimacov, 0..4 */
    float    e_h;       /* -1..1 */
    float    e_v;       /* -1..1 */
    float    pan_deg;
    float    tilt_deg;
    uint8_t  mode;      /* fsm_mode_t */
    uint8_t  ctrl;      /* 0 STEP, 1 P, 2 PI */
    float    lamp_deg;
    float    pv_v;
    float    pv_ma;
    float    pv_mw;
    float    pv_j;
} telemetry_t;

void comm_init(void);
void comm_usart2_irq(void);     /* volat z USART2_IRQHandler */
void comm_process(void);        /* volat v hlavnej slucke */

bool comm_send(const char *s);
bool comm_sendf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));  /* bez %f */
void comm_send_telemetry(const telemetry_t *t);
bool comm_log_enabled(void);

#endif /* COMM_H */
