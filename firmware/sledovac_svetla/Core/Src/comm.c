/*
 * comm.c
 * UART prikazy a telemetria, protokol je v docs/protokol_uart.md
 */
#include "comm.h"
#include "comm_hooks.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---------- pristup k USART2 ---------- */

#ifdef COMM_HOST_TEST
#include "comm_host_hw.h"   /* nahrada hardveru pre test na PC */
#else
#include "main.h"

static inline void hw_rx_irq_enable(void)  { LL_USART_EnableIT_RXNE(USART2); }
static inline bool hw_rx_ready(void)       { return LL_USART_IsActiveFlag_RXNE(USART2); }
static inline uint8_t hw_rx_read(void)     { return LL_USART_ReceiveData8(USART2); }
static inline bool hw_tx_ready(void)       { return LL_USART_IsActiveFlag_TXE(USART2); }
static inline void hw_tx_write(uint8_t b)  { LL_USART_TransmitData8(USART2, b); }
static inline bool hw_tx_irq_enabled(void) { return LL_USART_IsEnabledIT_TXE(USART2); }

static inline void hw_tx_irq(bool on)
{
    if (on) LL_USART_EnableIT_TXE(USART2);
    else    LL_USART_DisableIT_TXE(USART2);
}

/* pri chybe (hlavne ORE) by sa prerusenie volalo stale dookola */
static inline void hw_clear_errors(void)
{
    if (LL_USART_IsActiveFlag_ORE(USART2)) LL_USART_ClearFlag_ORE(USART2);
    if (LL_USART_IsActiveFlag_FE(USART2))  LL_USART_ClearFlag_FE(USART2);
    if (LL_USART_IsActiveFlag_NE(USART2))  LL_USART_ClearFlag_NE(USART2);
}
#endif

/* ---------- kruhovy buffer ---------- */

/* head zapisuje iba producent, tail iba konzument -> netreba vypinat prerusenia */
typedef struct {
    volatile uint8_t *buf;
    uint16_t size;              /* mocnina 2 */
    volatile uint16_t head;
    volatile uint16_t tail;
} ringbuf_t;

static volatile uint8_t rx_mem[COMM_RX_BUF_SIZE];
static volatile uint8_t tx_mem[COMM_TX_BUF_SIZE];
static ringbuf_t rx = { rx_mem, COMM_RX_BUF_SIZE, 0, 0 };
static ringbuf_t tx = { tx_mem, COMM_TX_BUF_SIZE, 0, 0 };

static uint16_t rb_count(const ringbuf_t *rb)
{
    return (uint16_t)(rb->head - rb->tail);
}

static bool rb_put(ringbuf_t *rb, uint8_t b)
{
    if (rb_count(rb) >= rb->size) return false;
    rb->buf[rb->head & (rb->size - 1u)] = b;
    rb->head++;
    return true;
}

static bool rb_get(ringbuf_t *rb, uint8_t *b)
{
    if (rb_count(rb) == 0u) return false;
    *b = rb->buf[rb->tail & (rb->size - 1u)];
    rb->tail++;
    return true;
}

/* zapise bud cely blok, alebo nic */
static bool rb_write(ringbuf_t *rb, const char *s, uint16_t len)
{
    if ((uint16_t)(rb->size - rb_count(rb)) < len) return false;
    uint16_t h = rb->head;
    for (uint16_t i = 0; i < len; i++) {
        rb->buf[(uint16_t)(h + i) & (rb->size - 1u)] = (uint8_t)s[i];
    }
    rb->head = (uint16_t)(h + len);
    return true;
}

/* ---------- stav modulu ---------- */

static char     line[COMM_LINE_MAX + 1u];
static uint16_t line_len;
static bool     line_too_long;

static bool log_on;
static bool hook_missing;

/* pocitadla pre ladenie (Live Expressions) */
static volatile uint32_t rx_dropped;
static uint32_t tx_dropped;

static const char TELEMETRY_HEADER[] =
    "#H t_ms,lh,ph,ld,pd,s,e_h,e_v,pan,tilt,mode,ctrl,lamp,u_mv,i_ua,p_uw,e_mj,rig_phase,overruns\r\n";

void comm_hook_missing(void)
{
    hook_missing = true;
}

/* ---------- verejne funkcie ---------- */

void comm_init(void)
{
    rx.head = rx.tail = 0;
    tx.head = tx.tail = 0;
    line_len = 0;
    line_too_long = false;
    log_on = false;
    rx_dropped = 0;
    tx_dropped = 0;
    hw_rx_irq_enable();
}

void comm_usart2_irq(void)
{
    hw_clear_errors();

    if (hw_rx_ready()) {
        uint8_t b = hw_rx_read();
        if (!rb_put(&rx, b)) rx_dropped++;
    }

    if (hw_tx_irq_enabled() && hw_tx_ready()) {
        uint8_t b;
        if (rb_get(&tx, &b)) hw_tx_write(b);
        else                 hw_tx_irq(false);
    }
}

bool comm_send(const char *s)
{
    size_t len = strlen(s);
    if (len == 0u) return true;
    if (len > COMM_TX_BUF_SIZE || !rb_write(&tx, s, (uint16_t)len)) {
        tx_dropped++;
        return false;
    }
    hw_tx_irq(true);
    return true;
}

bool comm_sendf(const char *fmt, ...)
{
    char buf[2u * COMM_LINE_MAX];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= sizeof buf) return false;
    return comm_send(buf);
}

bool comm_log_enabled(void)
{
    return log_on;
}

/* ---------- telemetria ---------- */

/* float * mierka -> zaokruhlene cele cislo */
static int32_t scale(float v, float k)
{
    float x = v * k;
    if (x != x)          return 0;              /* NaN */
    if (x > 2.0e9f)      return 2000000000;
    if (x < -2.0e9f)     return -2000000000;
    return (int32_t)(x >= 0.0f ? x + 0.5f : x - 0.5f);
}

static char *put_u32(char *p, uint32_t u)
{
    char tmp[10];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + (u % 10u));
        u /= 10u;
    } while (u != 0u);
    while (n > 0) *p++ = tmp[--n];
    return p;
}

static char *put_i32(char *p, int32_t v)
{
    if (v < 0) {
        *p++ = '-';
        return put_u32(p, 0u - (uint32_t)v);
    }
    return put_u32(p, (uint32_t)v);
}

void comm_send_telemetry(const telemetry_t *t)
{
    if (!log_on) return;

    char buf[224];              /* 19 cisel po max 11 znakov + ciarky + \r\n */
    char *p = buf;

    p = put_u32(p, t->t_ms);
    for (int i = 0; i < 4; i++) {
        *p++ = ','; p = put_i32(p, scale(t->ldr[i], 1000.0f));
    }
    *p++ = ','; p = put_i32(p, scale(t->s, 1000.0f));
    *p++ = ','; p = put_i32(p, scale(t->e_h, 1000.0f));
    *p++ = ','; p = put_i32(p, scale(t->e_v, 1000.0f));
    *p++ = ','; p = put_i32(p, scale(t->pan_deg, 10.0f));
    *p++ = ','; p = put_i32(p, scale(t->tilt_deg, 10.0f));
    *p++ = ','; p = put_u32(p, t->mode);
    *p++ = ','; p = put_u32(p, t->ctrl);
    *p++ = ','; p = put_i32(p, scale(t->lamp_deg, 10.0f));
    *p++ = ','; p = put_i32(p, scale(t->pv_v, 1000.0f));
    *p++ = ','; p = put_i32(p, scale(t->pv_ma, 1000.0f));
    *p++ = ','; p = put_i32(p, scale(t->pv_mw, 1000.0f));
    *p++ = ','; p = put_i32(p, scale(t->pv_j, 1000.0f));
    *p++ = ','; p = put_u32(p, t->rig_phase);
    *p++ = ','; p = put_u32(p, t->overruns);
    *p++ = '\r';
    *p++ = '\n';
    *p   = '\0';

    comm_send(buf);
}

/* ---------- prikazy ---------- */

typedef enum { R_OK, R_ARGS, R_RANGE, R_BUSY } result_t;

typedef struct {
    const char *name;
    uint8_t argc;               /* pocet slov vratane nazvu, 0 = kontroluje handler */
    result_t (*fn)(int argc, char **argv);
} command_t;

/* desatinne cislo bez exponentu, napr. -12.5 (strtof by pridal ~18 kB Flash) */
static bool parse_float(const char *s, float *out)
{
    bool neg = false;
    bool digits = false;
    float v = 0.0f;
    float frac = 0.1f;

    if (*s == '-' || *s == '+') {
        neg = (*s == '-');
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        v = v * 10.0f + (float)(*s - '0');
        if (v > 1.0e9f) return false;
        digits = true;
        s++;
    }
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') {
            v += frac * (float)(*s - '0');
            frac *= 0.1f;
            digits = true;
            s++;
        }
    }
    if (!digits || *s != '\0') return false;
    *out = neg ? -v : v;
    return true;
}

/* vrati index slova v zozname, alebo -1 */
static int find_word(const char *w, const char *const *list, int n)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(w, list[i]) == 0) return i;
    }
    return -1;
}

static result_t ok_if(bool r, result_t fail)
{
    return r ? R_OK : fail;
}

static result_t cmd_mode(int argc, char **argv)
{
    static const char *const names[] = { "TRACK", "AVOID", "SEARCH", "MANUAL", "CALIB" };
    (void)argc;
    int m = find_word(argv[1], names, 5);
    if (m < 0) return R_ARGS;
    return ok_if(fsm_request_mode((fsm_mode_t)m), R_BUSY);
}

static result_t cmd_goto(int argc, char **argv)
{
    float pan, tilt;
    (void)argc;
    if (!parse_float(argv[1], &pan) || !parse_float(argv[2], &tilt)) return R_ARGS;
    return ok_if(fsm_manual_goto(pan, tilt), R_RANGE);
}

static result_t cmd_ctrl(int argc, char **argv)
{
    static const char *const names[] = { "STEP", "P", "PI" };
    (void)argc;
    int t = find_word(argv[1], names, 3);
    if (t < 0) return R_ARGS;
    return ok_if(control_set_type((uint8_t)t), R_BUSY);
}

/* spolocna obsluha pre prikazy s jednym cislom */
static result_t one_float(char **argv, bool (*setter)(float))
{
    float v;
    if (!parse_float(argv[1], &v)) return R_ARGS;
    return ok_if(setter(v), R_RANGE);
}

static result_t cmd_kp(int argc, char **argv)   { (void)argc; return one_float(argv, control_set_kp); }
static result_t cmd_ki(int argc, char **argv)   { (void)argc; return one_float(argv, control_set_ki); }
static result_t cmd_step(int argc, char **argv) { (void)argc; return one_float(argv, control_set_step); }
static result_t cmd_db(int argc, char **argv)   { (void)argc; return one_float(argv, control_set_deadband); }
static result_t cmd_vmax(int argc, char **argv) { (void)argc; return one_float(argv, motion_set_vmax); }

static const char *const axes[] = { "PAN", "TILT" };   /* index = AXIS_x = CTRL_AXIS_x */

static result_t cmd_sign(int argc, char **argv)
{
    float s;
    (void)argc;
    int a = find_word(argv[1], axes, 2);
    if (a < 0) return R_ARGS;
    if (!parse_float(argv[2], &s) || (s != 1.0f && s != -1.0f)) return R_ARGS;
    return ok_if(control_set_sign((uint8_t)a, (int8_t)s), R_RANGE);
}

static result_t cmd_lim(int argc, char **argv)
{
    float lo, hi;
    (void)argc;
    int a = find_word(argv[1], axes, 2);
    if (a < 0) return R_ARGS;
    if (!parse_float(argv[2], &lo) || !parse_float(argv[3], &hi)) return R_ARGS;
    return ok_if(motion_set_limits((uint8_t)a, lo, hi), R_RANGE);
}

static result_t cmd_cal(int argc, char **argv)
{
    (void)argc;
    if (strcmp(argv[1], "DARK") == 0)   return ok_if(calib_capture_dark(), R_BUSY);
    if (strcmp(argv[1], "BRIGHT") == 0) return ok_if(calib_capture_bright(), R_BUSY);
    if (strcmp(argv[1], "SAVE") == 0)   return ok_if(calib_save(), R_BUSY);
    return R_ARGS;
}

static result_t cmd_rig(int argc, char **argv)
{
    float a, b;
    if (argc == 2 && strcmp(argv[1], "STOP") == 0) {
        return ok_if(testrig_stop(), R_BUSY);
    }
    if (argc == 3 && strcmp(argv[1], "RAMP") == 0) {
        if (!parse_float(argv[2], &a)) return R_ARGS;
        return ok_if(testrig_ramp(a), R_RANGE);
    }
    if (argc == 3 && strcmp(argv[1], "JUMP") == 0) {
        if (!parse_float(argv[2], &a)) return R_ARGS;
        return ok_if(testrig_jump(a), R_RANGE);
    }
    if (argc == 4 && strcmp(argv[1], "SINE") == 0) {
        if (!parse_float(argv[2], &a) || !parse_float(argv[3], &b)) return R_ARGS;
        return ok_if(testrig_sine(a, b), R_RANGE);
    }
    return R_ARGS;
}

static bool send_header;

static result_t cmd_log(int argc, char **argv)
{
    (void)argc;
    if (strcmp(argv[1], "ON") == 0) {
        log_on = true;
        send_header = true;
        return R_OK;
    }
    if (strcmp(argv[1], "OFF") == 0) {
        log_on = false;
        return R_OK;
    }
    return R_ARGS;
}

static result_t cmd_pv(int argc, char **argv)
{
    (void)argc;
    if (strcmp(argv[1], "RESET") != 0) return R_ARGS;
    return ok_if(ina219_reset_energy(), R_BUSY);
}

static result_t cmd_ping(int argc, char **argv)
{
    (void)argc; (void)argv;
    return R_OK;
}

static const command_t commands[] = {
    { "MODE", 2, cmd_mode },
    { "GOTO", 3, cmd_goto },
    { "CTRL", 2, cmd_ctrl },
    { "KP",   2, cmd_kp   },
    { "KI",   2, cmd_ki   },
    { "STEP", 2, cmd_step },
    { "DB",   2, cmd_db   },
    { "SIGN", 3, cmd_sign },
    { "VMAX", 2, cmd_vmax },
    { "LIM",  4, cmd_lim  },
    { "CAL",  2, cmd_cal  },
    { "RIG",  0, cmd_rig  },
    { "LOG",  2, cmd_log  },
    { "PV",   2, cmd_pv   },
    { "PING", 1, cmd_ping },
};

#define MAX_WORDS 6

static void execute(char *s)
{
    char *argv[MAX_WORDS];
    int argc = 0;

    for (char *c = s; *c; c++) {
        if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 'a' + 'A');
    }

    char *p = s;
    while (*p) {
        while (*p == ' ') p++;
        if (*p == '\0') break;
        if (argc == MAX_WORDS) {
            comm_sendf("#ERR ARGS %s\r\n", argv[0]);
            return;
        }
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    if (argc == 0) return;

    const command_t *cmd = NULL;
    for (size_t i = 0; i < sizeof commands / sizeof commands[0]; i++) {
        if (strcmp(argv[0], commands[i].name) == 0) {
            cmd = &commands[i];
            break;
        }
    }
    if (cmd == NULL) {
        comm_sendf("#ERR UNKNOWN %s\r\n", argv[0]);
        return;
    }
    if (cmd->argc != 0u && argc != cmd->argc) {
        comm_sendf("#ERR ARGS %s\r\n", argv[0]);
        return;
    }

    hook_missing = false;
    send_header = false;
    result_t r = cmd->fn(argc, argv);

    if (hook_missing) {
        comm_sendf("#ERR NOT_IMPL %s\r\n", argv[0]);
    } else if (r == R_OK) {
        comm_sendf("#OK %s\r\n", argv[0]);
        if (send_header) comm_send(TELEMETRY_HEADER);
    } else {
        static const char *const codes[] = { "OK", "ARGS", "RANGE", "BUSY" };
        comm_sendf("#ERR %s %s\r\n", codes[r], argv[0]);
    }
}

void comm_process(void)
{
    uint8_t c;

    while (rb_get(&rx, &c)) {
        if (c == '\r' || c == '\n') {
            if (line_too_long) {
                comm_send("#ERR TOO_LONG\r\n");
            } else if (line_len > 0u) {
                line[line_len] = '\0';
                execute(line);
            }
            line_len = 0;
            line_too_long = false;
        } else if (c == 0x08u || c == 0x7Fu) {     /* backspace */
            if (line_len > 0u && !line_too_long) line_len--;
        } else if (c >= 0x20u && c <= 0x7Eu) {
            if (line_len < COMM_LINE_MAX) line[line_len++] = (char)c;
            else                          line_too_long = true;
        }
    }
}
