/*
 * test_comm.c
 * Test comm.c na PC (bez STM32).
 *
 * Preklad a spustenie z priecinka firmware/tests:
 *   gcc -std=c11 -Wall -Wextra -DCOMM_HOST_TEST -I. -I../sledovac_svetla/Core/Inc
 *       test_comm.c ../sledovac_svetla/Core/Src/comm.c -o test_comm
 *   ./test_comm
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "comm.h"
#include "comm_hooks.h"

/* ---------- nahrada hardveru ---------- */

bool    mock_rx_ready;
uint8_t mock_rx_byte;
bool    mock_txe_irq;
char    mock_tx_out[8192];
size_t  mock_tx_len;

/* posle znaky do STM32, ako keby prisli z PC */
static void feed(const char *s)
{
    while (*s) {
        mock_rx_ready = true;
        mock_rx_byte = (uint8_t)*s++;
        comm_usart2_irq();
    }
}

/* vysle vsetko z TX buffera a vrati to ako retazec */
static const char *drain(void)
{
    mock_tx_len = 0;
    while (mock_txe_irq) comm_usart2_irq();
    mock_tx_out[mock_tx_len] = '\0';
    return mock_tx_out;
}

/* posle prikaz, spracuje ho a vrati odpoved */
static const char *cmd(const char *s)
{
    feed(s);
    comm_process();
    return drain();
}

/* ---------- nahrada funkcii inych modulov ---------- */

static float   last_f1, last_f2;
static int     last_i;
static int     calls;   /* pocet volani, na ladenie */

bool fsm_request_mode(fsm_mode_t m)          { last_i = (int)m; calls++; return true; }
bool fsm_manual_goto(float p, float t)       { last_f1 = p; last_f2 = t; calls++; return true; }
bool control_set_type(uint8_t t)             { last_i = t; calls++; return true; }
bool control_set_kp(float v)                 { last_f1 = v; calls++; return v >= 0.0f; }
bool control_set_ki(float v)                 { last_f1 = v; calls++; return v >= 0.0f; }
bool control_set_step(float v)               { last_f1 = v; calls++; return v > 0.0f; }
bool control_set_deadband(float v)           { last_f1 = v; calls++; return v >= 0.0f; }
bool control_set_sign(uint8_t a, int8_t s)   { last_i = a * 10 + s; calls++; return true; }
bool motion_set_vmax(float v)                { (void)v; comm_hook_missing(); return false; }
bool motion_set_limits(uint8_t a, float lo, float hi)
{
    last_i = a; last_f1 = lo; last_f2 = hi; calls++;
    return lo < hi;
}
bool sensors_set_lost_thresholds(float lo, float fo) { last_f1 = lo; last_f2 = fo; calls++; return lo >= 0.0f && lo < fo && fo <= 4.0f; }
bool filter_set_alpha(float a) { last_f1 = a; calls++; return a > 0.0f && a <= 1.0f; }
bool calib_capture_dark(void)                { calls++; return false; }
bool calib_capture_bright(void)              { calls++; return true; }
bool calib_save(void)                        { calls++; return true; }
bool testrig_ramp(float v)                   { last_f1 = v; calls++; return true; }
bool testrig_sine(float a, float p)          { last_f1 = a; last_f2 = p; calls++; return true; }
bool testrig_jump(float v)                   { last_f1 = v; calls++; return v >= 0.0f && v <= 180.0f; }
bool testrig_stop(void)                      { calls++; return true; }
bool ina219_reset_energy(void)               { calls++; return true; }

/* ---------- vyhodnotenie ---------- */

static int passed, failed;

static void check_str(const char *name, const char *got, const char *exp)
{
    if (strcmp(got, exp) == 0) {
        passed++;
    } else {
        failed++;
        printf("CHYBA %s\n  ocakavane: \"%s\"\n  dostal:    \"%s\"\n", name, exp, got);
    }
}

static void check(const char *name, bool ok)
{
    if (ok) passed++;
    else { failed++; printf("CHYBA %s\n", name); }
}

static bool near(float a, float b) { return a - b < 1e-5f && b - a < 1e-5f; }

/* ---------- testy ---------- */

static void test_basic(void)
{
    check_str("PING", cmd("PING\r"), "#OK PING\r\n");
    check_str("PING s CRLF", cmd("PING\r\n"), "#OK PING\r\n");
    check_str("iba LF", cmd("PING\n"), "#OK PING\r\n");
    check_str("prazdny riadok", cmd("\r\n\r\n"), "");
    check_str("medzery okolo", cmd("   ping   \r"), "#OK PING\r\n");
    check_str("dva prikazy naraz", cmd("PING\rPING\r"), "#OK PING\r\n#OK PING\r\n");
}

static void test_numbers(void)
{
    check_str("kp male pismena", cmd("kp 0.8\r"), "#OK KP\r\n");
    check(    "kp hodnota", near(last_f1, 0.8f));
    check_str("ki", cmd("KI 0.05\r"), "#OK KI\r\n");
    check(    "ki hodnota", near(last_f1, 0.05f));
    check_str("kp zaporne", cmd("KP -1\r"), "#ERR RANGE KP\r\n");
    check_str("kp text", cmd("KP abc\r"), "#ERR ARGS KP\r\n");
    check_str("kp 1.5x", cmd("KP 1.5x\r"), "#ERR ARGS KP\r\n");
    check_str("kp nan", cmd("KP nan\r"), "#ERR ARGS KP\r\n");
    check_str("kp bez cisla", cmd("KP\r"), "#ERR ARGS KP\r\n");
    check_str("kp dve cisla", cmd("KP 1 2\r"), "#ERR ARGS KP\r\n");
    check_str("step", cmd("STEP 2.5\r"), "#OK STEP\r\n");
    check_str("goto", cmd("GOTO 90 45.5\r"), "#OK GOTO\r\n");
    check(    "goto hodnoty", near(last_f1, 90.0f) && near(last_f2, 45.5f));
}

static void test_words(void)
{
    check_str("mode search", cmd("MODE SEARCH\r"), "#OK MODE\r\n");
    check( "mode hodnota", last_i == MODE_SEARCH);
    check_str("mode calib", cmd("mode calib\r"), "#OK MODE\r\n");
    check("mode calib hodnota", last_i == MODE_CALIB);
    check_str("mode zly", cmd("MODE FAST\r"), "#ERR ARGS MODE\r\n");
    check_str("ctrl pi", cmd("CTRL PI\r"), "#OK CTRL\r\n");
    check("ctrl hodnota", last_i == 2);
    check_str("lim tilt", cmd("LIM TILT 0 90\r"), "#OK LIM\r\n");
    check("lim hodnoty", last_i == AXIS_TILT && near(last_f1, 0.0f) && near(last_f2, 90.0f));
    check_str("lim obratene", cmd("LIM PAN 170 10\r"), "#ERR RANGE LIM\r\n");
    check_str("lim zla os", cmd("LIM ROLL 0 90\r"), "#ERR ARGS LIM\r\n");
    check_str("cal dark odmietnute", cmd("CAL DARK\r"), "#ERR BUSY CAL\r\n");
    check_str("cal save", cmd("CAL SAVE\r"), "#OK CAL\r\n");
    check_str("rig ramp", cmd("RIG RAMP 20\r"), "#OK RIG\r\n");
    check_str("rig sine", cmd("RIG SINE 30 4\r"), "#OK RIG\r\n");
    check("rig sine hodnoty", near(last_f1, 30.0f) && near(last_f2, 4.0f));
    check_str("rig stop", cmd("RIG STOP\r"), "#OK RIG\r\n");
    check_str("rig jump", cmd("RIG JUMP 120\r"), "#OK RIG\r\n");
    check("rig jump hodnota", near(last_f1, 120.0f));
    check_str("rig jump mimo", cmd("RIG JUMP 200\r"), "#ERR RANGE RIG\r\n");
    check_str("db", cmd("DB 0.02\r"), "#OK DB\r\n");
    check("db hodnota", near(last_f1, 0.02f));
    check_str("db zaporne", cmd("DB -1\r"), "#ERR RANGE DB\r\n");
    check_str("sign tilt -1", cmd("SIGN TILT -1\r"), "#OK SIGN\r\n");
    check("sign hodnota", last_i == 1 * 10 - 1);
    check_str("sign pan 1", cmd("sign pan 1\r"), "#OK SIGN\r\n");
    check("sign pan hodnota", last_i == 0 * 10 + 1);
    check_str("sign 2", cmd("SIGN PAN 2\r"), "#ERR ARGS SIGN\r\n");
    check_str("sign 0.5", cmd("SIGN PAN 0.5\r"), "#ERR ARGS SIGN\r\n");
    check_str("sign zla os", cmd("SIGN ROLL 1\r"), "#ERR ARGS SIGN\r\n");
    check_str("rig sine bez periody", cmd("RIG SINE 30\r"), "#ERR ARGS RIG\r\n");
    check_str("pv reset", cmd("PV RESET\r"), "#OK PV\r\n");
    check_str("pv zle", cmd("PV ZERO\r"), "#ERR ARGS PV\r\n");
    check_str("ema", cmd("EMA 0.2\r"), "#OK EMA\r\n");
    check("ema hodnota", near(last_f1, 0.2f));
    check_str("ema 0", cmd("EMA 0\r"), "#ERR RANGE EMA\r\n");
    check_str("ema 1.5", cmd("EMA 1.5\r"), "#ERR RANGE EMA\r\n");
    check_str("slost", cmd("SLOST 0.3 0.5\r"), "#OK SLOST\r\n");
    check("slost hodnoty", near(last_f1, 0.3f) && near(last_f2, 0.5f));
    check_str("slost obratene", cmd("SLOST 0.5 0.3\r"), "#ERR RANGE SLOST\r\n");
    check_str("slost jedno cislo", cmd("SLOST 0.3\r"), "#ERR ARGS SLOST\r\n");
    check_str("neznamy", cmd("FOO 1\r"), "#ERR UNKNOWN FOO\r\n");
    check_str("vmax chyba modul", cmd("VMAX 60\r"), "#ERR NOT_IMPL VMAX\r\n");
    check_str("po not_impl ide dalsi", cmd("PING\r"), "#OK PING\r\n");
}

static void test_line_editing(void)
{
    check_str("backspace", cmd("PINX\bG\r"), "#OK PING\r\n");
    check_str("delete", cmd("PINX\x7fG\r"), "#OK PING\r\n");
    check_str("tab ignorovany", cmd("PI\tNG\r"), "#OK PING\r\n");

    char longline[100];
    memset(longline, 'A', 80);
    strcpy(&longline[80], "\r");
    check_str("prilis dlhy", cmd(longline), "#ERR TOO_LONG\r\n");
    check_str("po dlhom ide dalsi", cmd("PING\r"), "#OK PING\r\n");

    check_str("vela slov", cmd("RIG SINE 1 2 3 4 5\r"), "#ERR ARGS RIG\r\n");
}

static void test_telemetry(void)
{
    telemetry_t t = {
        .t_ms = 123460,
        .ldr = { 0.512f, 0.498f, 0.430f, 0.441f },
        .s = 1.881f, .e_h = 0.0016f, .e_v = 0.0739f,
        .pan_deg = 90.3f, .tilt_deg = 45.2f,
        .mode = MODE_TRACK, .ctrl = 2, .lamp_deg = 95.0f,
        .pv_v = 5.12f, .pv_ma = 142.35f, .pv_mw = 728.832f, .pv_j = 1.534f,
        .rig_phase = 2, .overruns = 3,
    };

    comm_send_telemetry(&t);
    check_str("log vypnuty", drain(), "");

    check_str("log on", cmd("LOG ON\r"),
              "#OK LOG\r\n#H t_ms,lh,ph,ld,pd,s,e_h,e_v,pan,tilt,mode,ctrl,lamp,u_mv,i_ua,p_uw,e_mj,rig_phase,overruns\r\n");
    check( "log stav", comm_log_enabled());

    comm_send_telemetry(&t);
    check_str("riadok z dokumentacie", drain(),
              "123460,512,498,430,441,1881,2,74,903,452,0,2,950,5120,142350,728832,1534,2,3\r\n");

    telemetry_t n = { 0 };
    n.e_h = -0.5f; n.e_v = -0.0004f; n.pan_deg = -0.04f; n.pv_j = NAN;
    comm_send_telemetry(&n);
    check_str("zaporne a NaN", drain(), "0,0,0,0,0,0,-500,0,0,0,0,0,0,0,0,0,0,0,0\r\n");

    telemetry_t big = { 0 };
    big.t_ms = 4294967295u; big.pv_mw = 1.0e12f; big.e_h = -1.0e12f;
    big.rig_phase = 255; big.overruns = 4294967295u;
    comm_send_telemetry(&big);
    check_str("extremne hodnoty", drain(),
              "4294967295,0,0,0,0,0,-2000000000,0,0,0,0,0,0,0,0,2000000000,0,255,4294967295\r\n");

    check_str("log off", cmd("LOG OFF\r"), "#OK LOG\r\n");
    comm_send_telemetry(&t);
    check_str("po log off nic", drain(), "");
}

static void test_buffers(void)
{
    /* RX: viac znakov, nez sa zmesti, bez spracovania */
    for (int i = 0; i < 300; i++) feed("X");
    comm_process();
    feed("\r");
    comm_process();
    drain();
    check_str("po preteceni RX ide dalsi", cmd("PING\r"), "#OK PING\r\n");

    /* TX: bez vysielania sa buffer zaplni, riadky sa nesmu rozdelit */
    cmd("LOG ON\r");
    telemetry_t t = { .t_ms = 1, .s = 1.0f };
    const size_t line = strlen("1,0,0,0,0,1000,0,0,0,0,0,0,0,0,0,0,0,0,0\r\n");
    int ok = 0;
    /* comm_usart2_irq() sa tu nevola, takze sa nic nevysiela */
    for (int i = 0; i < 100; i++) {
        if (comm_send("1,0,0,0,0,1000,0,0,0,0,0,0,0,0,0,0,0,0,0\r\n")) ok++;
    }
    const char *out = drain();
    check("TX plny vrati false", ok < 100 && ok > 0);
    check("TX iba cele riadky", strlen(out) == (size_t)ok * line);
    comm_send_telemetry(&t);
    check_str("po vyprazdneni TX ide dalsi", drain(), "1,0,0,0,0,1000,0,0,0,0,0,0,0,0,0,0,0,0,0\r\n");
    cmd("LOG OFF\r");
}

int main(void)
{
    comm_init();
    test_basic();
    test_numbers();
    test_words();
    test_line_editing();
    test_telemetry();
    test_buffers();

    printf("\n%d testov preslo, %d zlyhalo\n", passed, failed);
    return failed == 0 ? 0 : 1;
}