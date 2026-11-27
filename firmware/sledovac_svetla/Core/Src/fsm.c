/*
 * fsm.c
 * Stavovy automat rezimov TRACK / AVOID / SEARCH / MANUAL / CALIB
 *
 * Zatial iba stav rezimu a jeho prepinanie. fsm_tick() pride v dalsom kroku.
 * Vsetko sa vola z hlavnej slucky (comm_process, neskor fsm_tick),
 * preto stav netreba chranit proti preruseniu.
 */
#include "fsm.h"
#include "comm.h"
#include "control.h"

static const char *const mode_names[MODE_COUNT] = {
    "TRACK", "AVOID", "SEARCH", "MANUAL", "CALIB"
};

/* po starte stoji, servami hybe az prikaz alebo tlacidlo */
static fsm_mode_t mode = MODE_MANUAL;

static void set_mode(fsm_mode_t m)
{
    if (m == mode) return;
    mode = m;
    control_reset();            /* pri kazdej zmene, dohoda s Maszayom */
    comm_sendf("#EV MODE %s\r\n", mode_names[m]);
}

void fsm_init(void)
{
    mode = MODE_MANUAL;
    control_reset();
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
