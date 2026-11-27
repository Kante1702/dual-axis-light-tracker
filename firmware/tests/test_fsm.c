/*
 * test_fsm.c
 * Test fsm.c na PC (bez STM32): rezimy, tick z TIM6, tlacidlo, LED, telemetria.
 *
 * Preklad a spustenie z priecinka firmware/tests:
 *   gcc -std=c11 -Wall -Wextra -DCOMM_HOST_TEST -DFSM_HOST_TEST -I. -I../sledovac_svetla/Core/Inc
 *       test_fsm.c ../sledovac_svetla/Core/Src/fsm.c ../sledovac_svetla/Core/Src/comm.c
 *       ../sledovac_svetla/Core/Src/comm_hooks.c -o test_fsm
 *   ./test_fsm
 */
#include <stdio.h>
#include <string.h>
#include "comm.h"
#include "fsm.h"
#include "control.h"
#include "testrig.h"

 /* ---------- nahrada hardveru ---------- */

bool    mock_rx_ready;
uint8_t mock_rx_byte;
bool    mock_txe_irq;
char    mock_tx_out[16384];
size_t  mock_tx_len;

bool mock_tim_flag;
bool mock_tim_started;
bool mock_btn_down;
bool mock_led;

static void feed(const char* s)
{
    while (*s) {
        mock_rx_ready = true;
        mock_rx_byte = (uint8_t)*s++;
        comm_usart2_irq();
    }
}

static const char* drain(void)
{
    mock_tx_len = 0;
    while (mock_txe_irq) comm_usart2_irq();
    mock_tx_out[mock_tx_len] = '\0';
    return mock_tx_out;
}

static const char* cmd(const char* s)
{
    feed(s);
    comm_process();
    return drain();
}

/* jeden tick: TIM6 nastavi priznak, prerusenie, hlavna slucka */
static void tick(void)
{
    mock_tim_flag = true;
    fsm_tim6_irq();
    fsm_process();
}

static void ticks(int n)
{
    for (int i = 0; i < n; i++) tick();
}

/* stlacenie tlacidla: zostupna hrana (EXTI) a tlacidlo drzane pocas ticku */
static void press(void)
{
    mock_btn_down = true;
    fsm_on_button();
    tick();
    mock_btn_down = false;
}

/* ---------- nahrada control.c a testrig.c ---------- */

static int resets;
void control_reset(void) { resets++; }
ctrl_type_t control_get_type(void) { return CTRL_PI; }

static int rig_steps;
void testrig_step(void) { rig_steps++; }
float testrig_angle(void) { return 95.0f; }
rig_phase_t testrig_phase(void) { return RIG_RUN; }

/* ---------- vyhodnotenie ---------- */

static int passed, failed;

static void check_str(const char* name, const char* got, const char* exp)
{
    if (strcmp(got, exp) == 0) {
        passed++;
    }
    else {
        failed++;
        printf("CHYBA %s\n  ocakavane: \"%s\"\n  dostal:    \"%s\"\n", name, exp, got);
    }
}

static void check(const char* name, bool ok)
{
    if (ok) passed++;
    else { failed++; printf("CHYBA %s\n", name); }
}

/* ---------- testy ---------- */

static void test_modes(void)
{
    check("start v MANUAL", fsm_get_mode() == MODE_MANUAL);
    check("TIM6 spusteny", mock_tim_started);

    resets = 0;
    check_str("MODE CALIB", cmd("MODE CALIB\r"), "#EV MODE CALIB\r\n#OK MODE\r\n");
    check("reset pri zmene", resets == 1);
    check_str("rovnaky rezim", cmd("MODE CALIB\r"), "#OK MODE\r\n");
    check("bez resetu pri rovnakom", resets == 1);
    check_str("zly rezim", cmd("MODE FAST\r"), "#ERR ARGS MODE\r\n");
    check("mimo rozsahu", !fsm_request_mode(MODE_COUNT));
    check_str("GOTO este chyba", cmd("GOTO 90 45\r"), "#ERR NOT_IMPL GOTO\r\n");
    cmd("MODE MANUAL\r");
}

static void test_tick(void)
{
    rig_steps = 0;
    fsm_process();
    check("bez priznaku ziadny tick", rig_steps == 0);

    ticks(5);
    check("5 tickov", rig_steps == 5);
    check("bez nestihnutych", fsm_overruns() == 0);

    /* dve prerusenia bez spracovania v hlavnej slucke */
    mock_tim_flag = true; fsm_tim6_irq();
    mock_tim_flag = true; fsm_tim6_irq();
    check("nestihnuty tick", fsm_overruns() == 1);
    fsm_process();
    check("spracovany jeden", rig_steps == 6);

    mock_tim_flag = false; fsm_tim6_irq();
    fsm_process();
    check("ine prerusenie TIM6 nie je tick", rig_steps == 6);
}

static void test_button(void)
{
    cmd("MODE TRACK\r");
    ticks(20);
    drain();

    press();
    check_str("TRACK -> AVOID", drain(), "#EV MODE AVOID\r\n");

    press();
    check_str("zakmit do 300 ms ignorovany", drain(), "");

    ticks(15);
    press();
    check_str("AVOID -> MANUAL", drain(), "#EV MODE MANUAL\r\n");

    ticks(15);
    press();
    check_str("MANUAL -> TRACK", drain(), "#EV MODE TRACK\r\n");

    ticks(15);
    fsm_on_button();            /* hrana, ale pri ticku uz nie je stlacene */
    tick();
    check_str("kratky zakmit ignorovany", drain(), "");

    cmd("MODE SEARCH\r");
    ticks(15);
    press();
    check_str("SEARCH -> TRACK", drain(), "#EV MODE TRACK\r\n");
    check("rezim TRACK", fsm_get_mode() == MODE_TRACK);
}

static int led_on_count(int n)
{
    int on = 0;
    for (int i = 0; i < n; i++) { tick(); on += mock_led; }
    return on;
}

static void test_led(void)
{
    cmd("MODE TRACK\r");
    check("TRACK stale svieti", led_on_count(100) == 100);
    cmd("MODE AVOID\r");
    check("AVOID 50 %", led_on_count(100) == 50);
    cmd("MODE MANUAL\r");
    check("MANUAL 50 %", led_on_count(100) == 50);
    cmd("MODE CALIB\r");
    check("CALIB 10 %", led_on_count(100) == 10);
    drain();
}

static void test_telemetry(void)
{
    cmd("MODE AVOID\r");
    drain();
    check_str("LOG ON", cmd("LOG ON\r"),
        "#OK LOG\r\n#H t_ms,lh,ph,ld,pd,s,e_h,e_v,pan,tilt,mode,ctrl,lamp,u_mv,i_ua,p_uw,e_mj,rig_phase,overruns\r\n");

    tick();
    const char* line = drain();
    unsigned t_ms;
    int n = sscanf(line, "%u,", &t_ms);
    check("riadok zacina casom", n == 1 && t_ms % 20u == 0u);
    const char* rest = strchr(line, ',');
    check_str("riadok telemetrie", rest ? rest : "",
        ",0,0,0,0,0,0,0,0,0,1,2,950,0,0,0,0,3,1\r\n");

    tick();
    unsigned t2;
    sscanf(drain(), "%u,", &t2);
    check("cas +20 ms", t2 == t_ms + 20u);
    cmd("LOG OFF\r");
}

int main(void)
{
    comm_init();
    fsm_init();

    test_modes();
    test_tick();
    test_button();
    test_led();
    test_telemetry();

    printf("\n%d testov preslo, %d zlyhalo\n", passed, failed);
    return failed == 0 ? 0 : 1;
}