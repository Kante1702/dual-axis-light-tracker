/*
 * comm.h
 * UART: príjem s prerušením do kruhového buffera, príkazy, telemetria
 * Zodpovedá: člen 4 (Kanta)
 *
 * Protokol (príkazy, odpovede, stĺpce telemetrie) je popísaný
 * v docs/protokol_uart.md.
 *
 * Kontext volaní:
 *   comm_usart2_irq()  - iba z USART2_IRQHandler
 *   všetky ostatné     - iba z hlavnej slučky (aj z fsm_tick()),
 *                        nikdy z iného prerušenia
 */
#ifndef COMM_H
#define COMM_H

#include <stdint.h>
#include <stdbool.h>

/* Veľkosti bufferov v bajtoch (mocnina 2 kvôli rýchlemu modulu).
 * TX pojme niekoľko riadkov telemetrie aj odpovede na príkazy. */
#define COMM_RX_BUF_SIZE   128u
#define COMM_TX_BUF_SIZE   512u
#define COMM_LINE_MAX       64u   /* max. dĺžka jedného príkazu */

/*
 * Jeden riadok telemetrie vo fyzikálnych jednotkách.
 * Každý modul dodá hodnoty tak, ako ich sám počíta (float).
 * Prevod na celé čísla s pevnou mierkou a formátovanie robí
 * comm_send_telemetry(), mierky sú v docs/protokol_uart.md.
 */
typedef struct {
    uint32_t t_ms;      /* čas od štartu [ms]                              */
    float    ldr[4];    /* LH, PH, LD, PD po kalibrácii a filtrácii, 0..1  */
    float    s;         /* súčet snímačov S, 0..4                          */
    float    e_h;       /* horizontálna odchýlka, -1..1                    */
    float    e_v;       /* vertikálna odchýlka, -1..1                      */
    float    pan_deg;   /* zadaný uhol serva pan [°]                       */
    float    tilt_deg;  /* zadaný uhol serva tilt [°]                      */
    uint8_t  mode;      /* aktuálny režim, hodnota fsm_mode_t              */
    uint8_t  ctrl;      /* typ regulátora: 0 = STEP, 1 = P, 2 = PI         */
    float    lamp_deg;  /* uhol lampy na prípravku [°]                     */
    float    pv_v;      /* napätie panelu [V]                              */
    float    pv_ma;     /* prúd panelu po filtrácii [mA]                   */
    float    pv_mw;     /* výkon panelu [mW]                               */
    float    pv_j;      /* energia od posledného PV RESET [J]              */
} telemetry_t;

/* Vynuluje buffre a zapne prerušenie RXNE na USART2. */
void comm_init(void);

/* Obsluha prerušenia USART2 (RXNE: znak do RX buffera,
 * TXE: ďalší znak z TX buffera). Volá ju USART2_IRQHandler. */
void comm_usart2_irq(void);

/* Volať v každom prechode hlavnej slučky. Vyberie prijaté znaky,
 * poskladá riadok a vykoná príkaz (odpoveď #OK / #ERR). */
void comm_process(void);

/* Vloží reťazec do TX buffera, neblokuje. Ak sa nezmestí celý,
 * nevloží nič a vráti false (riadky sa nikdy nepremiešajú). */
bool comm_send(const char *s);

/* Ako comm_send, ale s formátovaním printf.
 * Iba celé čísla a reťazce, %f newlib-nano bez _printf_float nevypíše. */
bool comm_sendf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Ak je zapnutý LOG ON, pošle jeden riadok telemetrie.
 * Volá fsm_tick() raz za tick (20 ms). */
void comm_send_telemetry(const telemetry_t *t);

/* true, ak je zapnuté posielanie telemetrie (príkaz LOG ON). */
bool comm_log_enabled(void);

#endif /* COMM_H */
