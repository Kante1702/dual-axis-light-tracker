/*
 * fsm.c
 * Stavovy automat rezimov TRACK / AVOID / SEARCH / MANUAL / CALIB
 *
 * TIM6 kazdych 20 ms v preruseni iba nastavi priznak, fsm_tick() bezi
 * v hlavnej slucke spolu s comm_process(). Preto sa navzajom neprerusia
 * a stav rezimu netreba chranit.
 *
 * Snimace (Kiraly) a serva (Marcinek) sa doplnia do fsm_tick(),
 * ked budu ich funkcie v main (miesta oznacene TODO).
 */
#include "fsm.h"
#include "comm.h"
#include "control.h"
#include "testrig.h"

/* ---------- pristup k hardveru ---------- */

#ifdef FSM_HOST_TEST
#include "fsm_host_hw.h"    /* nahrada hardveru pre test na PC */
#else
#include "main.h"

static inline void hw_tick_start(void)
{
    LL_TIM_ClearFlag_UPDATE(TIM6);      /* LL_TIM_Init ho nastavi hned pri inicializacii */
    LL_TIM_EnableIT_UPDATE(TIM6);
    LL_TIM_EnableCounter(TIM6);
}
static inline bool hw_tick_flag(void)   { return LL_TIM_IsActiveFlag_UPDATE(TIM6); }
static inline void hw_tick_clear(void)  { LL_TIM_ClearFlag_UPDATE(TIM6); }
static inline bool hw_button_down(void) { return !LL_GPIO_IsInputPinSet(BTN_GPIO_Port, BTN_Pin); }

static inline void hw_led(bool on)
{
    if (on) LL_GPIO_SetOutputPin(LED_GPIO_Port, LED_Pin);
    else    LL_GPIO_ResetOutputPin(LED_GPIO_Port, LED_Pin);
}
#endif

/* ---------- nastavenia ---------- */

#define TICK_MS          20u
#define BTN_LOCKOUT      15u    /* 300 ms po stlaceni sa dalsie ignoruje */

/* LED podla rezimu: perioda a cas svietenia v tickoch */
typedef struct { uint8_t period; uint8_t on; } led_pattern_t;

static const led_pattern_t led_patterns[MODE_COUNT] = {
    [MODE_TRACK]  = {  1,  1 },     /* stale svieti */
    [MODE_AVOID]  = { 10,  5 },     /* rychlo, 5 Hz */
    [MODE_SEARCH] = { 25, 12 },     /* 2 Hz */
    [MODE_MANUAL] = { 50, 25 },     /* pomaly, 1 Hz */
    [MODE_CALIB]  = { 50,  5 },     /* kratky zablesk raz za sekundu */
};

static const char *const mode_names[MODE_COUNT] = {
    "TRACK", "AVOID", "SEARCH", "MANUAL", "CALIB"
};

/* ---------- stav ---------- */

/* po starte stoji, servami hybe az prikaz alebo tlacidlo */
static fsm_mode_t mode = MODE_MANUAL;

static uint32_t tick_count;
static uint8_t  btn_lockout;

/* nastavuju prerusenia */
static volatile bool     tick_pending;
static volatile bool     btn_pressed;
static volatile uint32_t overruns;

/* ---------- rezimy ---------- */

static void set_mode(fsm_mode_t m)
{
    if (m == mode) return;
    mode = m;
    control_reset();            /* pri kazdej zmene, dohoda s Maszayom */
    comm_sendf("#EV MODE %s\r\n", mode_names[m]);
}

/* tlacidlo: TRACK -> AVOID -> MANUAL -> TRACK, zo SEARCH a CALIB do TRACK */
static fsm_mode_t next_mode_on_button(fsm_mode_t m)
{
    switch (m) {
    case MODE_TRACK: return MODE_AVOID;
    case MODE_AVOID: return MODE_MANUAL;
    default:         return MODE_TRACK;
    }
}

static void handle_button(void)
{
    if (btn_lockout > 0u) btn_lockout--;

    if (!btn_pressed) return;
    btn_pressed = false;

    /* zakmity: ignoruj, ak je zamknute alebo uz tlacidlo nie je stlacene */
    if (btn_lockout > 0u || !hw_button_down()) return;

    btn_lockout = BTN_LOCKOUT;
    set_mode(next_mode_on_button(mode));
}

static void update_led(void)
{
    const led_pattern_t *p = &led_patterns[mode];
    hw_led((tick_count % p->period) < p->on);
}

static void send_telemetry(void)
{
    telemetry_t t = { 0 };

    t.t_ms      = tick_count * TICK_MS;
    t.mode      = (uint8_t)mode;
    t.ctrl      = (uint8_t)control_get_type();
    t.lamp_deg  = testrig_angle();
    t.rig_phase = (uint8_t)testrig_phase();
    t.overruns  = overruns;
    /* TODO Kiraly: ldr[4], s, e_h, e_v zo sensors_get() */
    /* TODO Marcinek: pan_deg, tilt_deg z motion_get_angle() */
    /* TODO ina219: pv_v, pv_ma, pv_mw, pv_j */

    comm_send_telemetry(&t);
}

/* ---------- verejne funkcie ---------- */

void fsm_init(void)
{
    mode = MODE_MANUAL;
    tick_count = 0;
    btn_lockout = 0;
    tick_pending = false;
    btn_pressed = false;
    overruns = 0;
    control_reset();
    hw_tick_start();
}

fsm_mode_t fsm_get_mode(void)
{
    return mode;
}

bool fsm_request_mode(fsm_mode_t m)
{
    if ((unsigned)m >= (unsigned)MODE_COUNT) return false;
    set_mode(m);
    return true;
}

uint32_t fsm_overruns(void)
{
    return overruns;
}

void fsm_tim6_irq(void)
{
    if (!hw_tick_flag()) return;
    hw_tick_clear();
    if (tick_pending) overruns++;       /* predchadzajuci tick sa este nespracoval */
    tick_pending = true;
}

void fsm_on_button(void)
{
    btn_pressed = true;
}

void fsm_process(void)
{
    if (!tick_pending) return;
    tick_pending = false;
    fsm_tick();
}

void fsm_tick(void)
{
    tick_count++;

    handle_button();

    /* TODO Kiraly: sensors_get(&s); */

    switch (mode) {
    case MODE_TRACK:
    case MODE_AVOID:
        /* TODO: control_update(...) a motion_set_target(...) podla dohody s Maszayom */
        break;
    case MODE_SEARCH:
        /* TODO krok 7: search_step() */
        break;
    case MODE_MANUAL:       /* ciel nastavi GOTO */
    case MODE_CALIB:        /* kalibraciu riadi Kiraly prikazmi CAL */
    default:
        break;
    }

    /* TODO Marcinek: motion_update(); */
    testrig_step();
    /* TODO Marcinek: servo_set_angle(SERVO_LAMP, testrig_angle()); */

    update_led();
    send_telemetry();
}
