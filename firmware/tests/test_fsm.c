/*
 * test_fsm.c
 * Test prepinania rezimov vo fsm.c na PC (bez STM32).
 *
 * Preklad a spustenie z priecinka firmware/tests:
 *   gcc -std=c11 -Wall -Wextra -DCOMM_HOST_TEST -I. -I../sledovac_svetla/Core/Inc
 *       test_fsm.c ../sledovac_svetla/Core/Src/fsm.c ../sledovac_svetla/Core/Src/comm.c
 *       ../sledovac_svetla/Core/Src/comm_hooks.c -o test_fsm
 *   ./test_fsm
 */
#include <stdio.h>
#include <string.h>
#include "comm.h"
#include "fsm.h"

 /* ---------- nahrada hardveru ---------- */

bool    mock_rx_ready;
uint8_t mock_rx_byte;
bool    mock_txe_irq;
char    mock_tx_out[4096];
size_t  mock_tx_len;

static const char* cmd(const char* s)
{
    while (*s) {
        mock_rx_ready = true;
        mock_rx_byte = (uint8_t)*s++;
        comm_usart2_irq();
    }
    comm_process();
    mock_tx_len = 0;
    while (mock_txe_irq) comm_usart2_irq();
    mock_tx_out[mock_tx_len] = '\0';
    return mock_tx_out;
}

/* ---------- nahrada control.c ---------- */

static int resets;
void control_reset(void) { resets++; }

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

int main(void)
{
    comm_init();
    fsm_init();

    check("start v MANUAL", fsm_get_mode() == MODE_MANUAL);

    resets = 0;
    check_str("MODE CALIB", cmd("MODE CALIB\r"), "#EV MODE CALIB\r\n#OK MODE\r\n");
    check("rezim CALIB", fsm_get_mode() == MODE_CALIB);
    check("reset pri zmene", resets == 1);

    check_str("rovnaky rezim", cmd("MODE CALIB\r"), "#OK MODE\r\n");
    check("bez resetu pri rovnakom", resets == 1);

    check_str("TRACK", cmd("mode track\r"), "#EV MODE TRACK\r\n#OK MODE\r\n");
    check_str("AVOID", cmd("MODE AVOID\r"), "#EV MODE AVOID\r\n#OK MODE\r\n");
    check("reset pri TRACK -> AVOID", resets == 3);
    check("rezim AVOID", fsm_get_mode() == MODE_AVOID);

    check_str("zly rezim", cmd("MODE FAST\r"), "#ERR ARGS MODE\r\n");
    check("rezim sa nezmenil", fsm_get_mode() == MODE_AVOID);

    check("mimo rozsahu", !fsm_request_mode(MODE_COUNT));
    check("mimo rozsahu nic nezmeni", fsm_get_mode() == MODE_AVOID && resets == 3);

    check_str("GOTO este chyba", cmd("GOTO 90 45\r"), "#ERR NOT_IMPL GOTO\r\n");

    printf("\n%d testov preslo, %d zlyhalo\n", passed, failed);
    return failed == 0 ? 0 : 1;
}