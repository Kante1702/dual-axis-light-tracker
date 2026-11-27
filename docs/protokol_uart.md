# UART protokol a telemetria

Zodpovedá: Kanta (`comm.c`, `tools/logger.py`)

Tento súbor je dohoda medzi firmvérom a Python skriptmi. Ak sa tu niečo mení, treba upraviť aj `comm.c` a `tools/logger.py`.

## 1. Spojenie

- USART2 cez ST-LINK (virtuálny COM port), piny PA2 / PA15
- **115200 Bd, 8N1**, bez riadenia toku
- iba ASCII znaky

## 2. Príkazy z PC

Pravidlá:
- jeden príkaz na riadok, ukončený `\r`, `\n` alebo `\r\n` (PuTTY posiela pri Enteri iba `\r`)
- veľké a malé písmená sa nerozlišujú, `kp 0.8` je to isté ako `KP 0.8`
- slová oddeľuje jedna alebo viac medzier
- desatinné čísla s bodkou, bez exponentu, napríklad `0.8` alebo `-12.5`
- riadok dlhší ako 64 znakov sa zahodí s odpoveďou `#ERR TOO_LONG`
- prázdny riadok sa ignoruje

`comm.c` iba rozparsuje príkaz a zavolá funkciu modulu, ktorý ho vykoná. Rozsah hodnôt kontroluje modul: funkcia vráti `false` a `comm.c` odpovie `#ERR RANGE`. Názvy funkcií v tabuľke sú návrh na dohodu, ich deklarácie sú v `comm_hooks.h`.

Kým modul funkciu nemá, použije sa dočasná (weak) verzia z `comm_hooks.c` a príkaz odpovie `#ERR NOT_IMPL`. Keď člen tímu napíše vo svojom module funkciu s rovnakým menom a parametrami, automaticky sa použije jeho verzia.

| Príkaz | Čo robí | Volá | Modul |
|---|---|---|---|
| `MODE TRACK\|AVOID\|SEARCH\|MANUAL\|CALIB` | zmena režimu | `fsm_request_mode(m)` | Kanta |
| `GOTO <pan> <tilt>` | prepne do MANUAL a natočí hlavu | `fsm_manual_goto(pan, tilt)` | Kanta |
| `CTRL STEP\|P\|PI` | typ regulátora | `control_set_type(t)` | Maszay |
| `KP <x>` | zosilnenie P | `control_set_kp(x)` | Maszay |
| `KI <x>` | zosilnenie I | `control_set_ki(x)` | Maszay |
| `STEP <deg>` | krok Δ krokového riadenia | `control_set_step(deg)` | Maszay |
| `DB <x>` | pásmo necitlivosti regulátora | `control_set_deadband(x)` | Maszay |
| `SIGN PAN\|TILT 1\|-1` | otočenie smeru osi regulátora | `control_set_sign(os, znamienko)` | Maszay |
| `VMAX <deg/s>` | max. rýchlosť serv | `motion_set_vmax(v)` | Marcinek |
| `LIM PAN <min> <max>` | rozsah pan | `motion_set_limits(AXIS_PAN, min, max)` | Marcinek |
| `LIM TILT <min> <max>` | rozsah tilt | `motion_set_limits(AXIS_TILT, min, max)` | Marcinek |
| `CAL DARK\|BRIGHT\|SAVE` | kalibrácia snímačov | `calib_capture_dark()`, `calib_capture_bright()`, `calib_save()` | Király |
| `EMA <alfa>` | koeficient EMA filtra snímačov, 0 < alfa ≤ 1 | `filter_set_alpha(alfa)` | Király |
| `SLOST <lost> <found>` | prahy straty a nájdenia zdroja podľa S, 0 ≤ lost < found ≤ 4 | `sensors_set_lost_thresholds(lost, found)` | Király |
| `RIG RAMP <deg/s>` | lampa ide rampou | `testrig_ramp(v)` | Maszay |
| `RIG SINE <amp> <perioda>` | lampa ide sínusom | `testrig_sine(amp, perioda)` | Maszay |
| `RIG JUMP <deg>` | skok lampy na uhol | `testrig_jump(deg)` | Maszay |
| `RIG STOP` | zastaví lampu | `testrig_stop()` | Maszay |
| `LOG ON\|OFF` | zapne / vypne telemetriu | interné v `comm.c` | Kanta |
| `PV RESET` | vynuluje energiu panelu | `ina219_reset_energy()` | Kanta |
| `PING` | test spojenia | interné v `comm.c` | Kanta |

## 3. Riadky zo STM32

Riadok, ktorý začína číslicou, je telemetria. Každý iný riadok začína znakom `#`:

| Začiatok | Význam | Príklad |
|---|---|---|
| `#H` | hlavička telemetrie, posiela sa po `LOG ON` | `#H t_ms,lh,ph,...` |
| `#OK` | príkaz sa vykonal | `#OK KP` |
| `#ERR` | príkaz sa nevykonal | `#ERR RANGE KP` |
| `#EV` | udalosť, napr. zmena režimu (posiela sa pred `#OK`) | `#EV MODE SEARCH` |

Chybová odpoveď má tvar `#ERR <kód> <príkaz>`, iba `TOO_LONG` je bez príkazu. Kódy:

| Kód | Kedy |
|---|---|
| `UNKNOWN` | neznámy príkaz |
| `ARGS` | chýba argument alebo nie je číslo |
| `RANGE` | modul hodnotu odmietol |
| `BUSY` | modul príkaz bez čísla odmietol (napr. `CAL` mimo režimu CALIB) |
| `NOT_IMPL` | modul funkciu ešte nemá |
| `TOO_LONG` | riadok je dlhší ako 64 znakov |

## 4. Telemetria

Po príkaze `LOG ON` posiela STM32 hlavičku `#H ...` a potom jeden riadok v každom ticku, teda každých 20 ms (50 riadkov za sekundu).

Hodnoty sa posielajú ako **celé čísla s pevnou mierkou**, nie ako desatinné čísla:
- `printf` s `%f` na STM32 potrebuje ďalšie kilobajty Flash (`_printf_float`) a je pomalý,
- celé číslo je kratšie, takže sa do linky zmestí viac dát.

V Pythone stačí stĺpec vydeliť mierkou.

| # | Stĺpec | Veličina | Jednotka v CSV | Mierka | Rozsah |
|---|---|---|---|---|---|
| 1 | `t_ms` | čas od štartu | ms | 1 | 0 … |
| 2 | `lh` | snímač ľavý horný | ‰ | ×1000 | 0 … 1000 |
| 3 | `ph` | snímač pravý horný | ‰ | ×1000 | 0 … 1000 |
| 4 | `ld` | snímač ľavý dolný | ‰ | ×1000 | 0 … 1000 |
| 5 | `pd` | snímač pravý dolný | ‰ | ×1000 | 0 … 1000 |
| 6 | `s` | súčet S | ‰ | ×1000 | 0 … 4000 |
| 7 | `e_h` | horizontálna odchýlka | ‰ | ×1000 | −1000 … 1000 |
| 8 | `e_v` | vertikálna odchýlka | ‰ | ×1000 | −1000 … 1000 |
| 9 | `pan` | zadaný uhol pan | 0,1° | ×10 | 0 … 1800 |
| 10 | `tilt` | zadaný uhol tilt | 0,1° | ×10 | 0 … 1800 |
| 11 | `mode` | režim | kód | – | 0 TRACK, 1 AVOID, 2 SEARCH, 3 MANUAL, 4 CALIB |
| 12 | `ctrl` | regulátor | kód | – | 0 STEP, 1 P, 2 PI |
| 13 | `lamp` | uhol lampy | 0,1° | ×10 | 0 … 1800 |
| 14 | `u_mv` | napätie panelu | mV | ×1000 | 0 … 26000 |
| 15 | `i_ua` | prúd panelu | µA | ×1000 | 0 … 400000 |
| 16 | `p_uw` | výkon panelu | µW | ×1000 | 0 … |
| 17 | `e_mj` | energia od `PV RESET` | mJ | ×1000 | 0 … |
| 18 | `rig_phase` | fáza prípravku, `testrig_phase()` | kód | – | podľa `testrig.h` |
| 19 | `overruns` | počet nestihnutých tickov od štartu | – | 1 | 0 … |

Mierka znamená, akým číslom STM32 násobí hodnotu z `telemetry_t`. V Pythone sa ním hodnota delí.

Príklad:

```
#H t_ms,lh,ph,ld,pd,s,e_h,e_v,pan,tilt,mode,ctrl,lamp,u_mv,i_ua,p_uw,e_mj,rig_phase,overruns
123460,512,498,430,441,1881,2,74,903,452,0,2,950,5120,142350,728832,1534,2,3
```

Znamená to: čas 123,46 s, S = 1,881, e_h = 0,002, e_v = 0,074, pan 90,3°, tilt 45,2°, režim TRACK, regulátor PI, lampa 95,0°, panel 5,12 V, 142,35 mA, 728,8 mW, od nulovania 1,534 J, prípravok vo fáze 2 a doteraz 3 nestihnuté ticky.

`pan` a `tilt` sú uhly, ktoré posiela firmvér do serv, nie namerané polohy. Servo spätnú väzbu nemá.

### Priepustnosť

Riadok s najdlhšími možnými hodnotami má najviac 110 bajtov vrátane `\r\n`, bežný riadok okolo 80 bajtov:
- 115200 Bd pri 8N1 prenesie 11 520 B/s, 50 riadkov × 110 B = 5500 B/s, teda linka je vyťažená najviac na **48 %**,
- jeden riadok trvá na linke ~9,5 ms. Keby sa posielal blokujúco, zabral by takmer polovicu 20 ms ticku. Preto sa vysiela cez TX buffer a prerušenie TXE,
- pri 38400 Bd (~3800 B/s) by sa telemetria do linky **nezmestila**.

## 5. Pravidlá zmien

- Nové stĺpce sa pridávajú **iba na koniec**. Poradie ani mierky existujúcich stĺpcov sa nemenia.
- `tools/logger.py` uloží CSV s hlavičkou z riadku `#H` a skripty čítajú stĺpce podľa názvu. Pridaný stĺpec ich preto nerozbije.
- Nové príkazy a stĺpce sa zapíšu sem a do tabuľky v README.

## 6. Otvorené body na dohodu

1. **Kde beží `fsm_tick()`.** Návrh: TIM6 prerušenie iba nastaví príznak a `fsm_tick()` aj `comm_process()` bežia v hlavnej slučke. Potom príkazy z UART môžu meniť parametre regulátora bez ochrany proti súbehu. Ak to schválime, zmeníme vetu v README „regulácia beží v prerušení časovača“.
2. **Király:** dohodnuté 26. 11.: `ch[]` po kalibrácii nominálne 0 … 1 (zhora sa neorezáva), S nominálne 0 … 4, strata zdroja pri S < 0,3, opätovné nájdenie pri S > 0,5 (príkaz `SLOST`). Otvorené: záznam surových dát do RAM príkazom `RAW <n>`.
3. **Maszay:** dohodnuté 19. 11.: príkazy `RIG JUMP`, `DB`, `SIGN`, stĺpce `rig_phase` a `overruns`, `CTRL_AXIS_PAN = 0`, `CTRL_AXIS_TILT = 1`. Dohodnuté 26. 11.: v `fsm_tick()` sa vždy volá `sensors_get(&s)`. Iba v TRACK/AVOID nasleduje `control_update(s.e_h, s.e_v, s.lost, s.seq, avoid, motion_get_sat(SERVO_PAN), motion_get_sat(SERVO_TILT), &d_pan, &d_tilt)` a `motion_set_target(motion_get_angle(SERVO_PAN) + d_pan, motion_get_angle(SERVO_TILT) + d_tilt)`. Vo všetkých režimoch potom `motion_update()`, `testrig_step()` a `servo_set_angle(SERVO_LAMP, testrig_angle())`; lampa ide priamo cez servo, nie cez motion. `control_reset()` pri každej zmene režimu. `motion_move()` a `control_feedback()` už neexistujú.
4. **Marcinek:** názvy `motion_set_vmax()`, `motion_set_limits()` a funkcia na zadanie cieľovej polohy pre `GOTO`.
5. **Meranie energie:** energia sa integruje iba keď `testrig_phase() == RIG_RUN`, výsledok sa pošle pri prechode do `RIG_DONE` (dohodnuté s Maszayom).
