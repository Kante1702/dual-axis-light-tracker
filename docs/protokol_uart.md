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
- desatinné čísla s bodkou, napríklad `0.8`
- riadok dlhší ako 64 znakov sa zahodí s odpoveďou `#ERR TOO_LONG`
- prázdny riadok sa ignoruje

`comm.c` iba rozparsuje príkaz a zavolá funkciu modulu, ktorý ho vykoná. Rozsah hodnôt kontroluje modul: funkcia vráti `false` a `comm.c` odpovie `#ERR RANGE`. Názvy funkcií v tabuľke sú návrh na dohodu.

| Príkaz | Čo robí | Volá | Modul |
|---|---|---|---|
| `MODE TRACK\|AVOID\|SEARCH\|MANUAL\|CALIB` | zmena režimu | `fsm_request_mode(m)` | Kanta |
| `GOTO <pan> <tilt>` | prepne do MANUAL a natočí hlavu | `fsm_manual_goto(pan, tilt)` | Kanta |
| `CTRL STEP\|P\|PI` | typ regulátora | `control_set_type(t)` | Maszay |
| `KP <x>` | zosilnenie P | `control_set_kp(x)` | Maszay |
| `KI <x>` | zosilnenie I | `control_set_ki(x)` | Maszay |
| `STEP <deg>` | krok Δ krokového riadenia | `control_set_step(deg)` | Maszay |
| `VMAX <deg/s>` | max. rýchlosť serv | `motion_set_vmax(v)` | Marcinek |
| `LIM PAN <min> <max>` | rozsah pan | `motion_set_limits(AXIS_PAN, min, max)` | Marcinek |
| `LIM TILT <min> <max>` | rozsah tilt | `motion_set_limits(AXIS_TILT, min, max)` | Marcinek |
| `CAL DARK\|BRIGHT\|SAVE` | kalibrácia snímačov | `calib_capture_dark()`, `calib_capture_bright()`, `calib_save()` | Király |
| `RIG RAMP <deg/s>` | lampa ide rampou | `testrig_ramp(v)` | Maszay |
| `RIG SINE <amp> <perioda>` | lampa ide sínusom | `testrig_sine(amp, perioda)` | Maszay |
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
| `#ERR` | príkaz sa nevykonal | `#ERR RANGE` |
| `#EV` | udalosť | `#EV MODE SEARCH` |

Chybové kódy:

| Kód | Kedy |
|---|---|
| `UNKNOWN` | neznámy príkaz |
| `ARGS` | chýba argument alebo nie je číslo |
| `RANGE` | modul hodnotu odmietol |
| `BUSY` | príkaz sa teraz nedá vykonať (napr. `CAL` mimo režimu CALIB) |
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

Mierka znamená, akým číslom STM32 násobí hodnotu z `telemetry_t`. V Pythone sa ním hodnota delí.

Príklad:

```
#H t_ms,lh,ph,ld,pd,s,e_h,e_v,pan,tilt,mode,ctrl,lamp,u_mv,i_ua,p_uw,e_mj
123460,512,498,430,441,1881,2,74,903,452,0,2,950,5120,142350,728832,1534
```

Znamená to: čas 123,46 s, S = 1,881, e_h = 0,002, e_v = 0,074, pan 90,3°, tilt 45,2°, režim TRACK, regulátor PI, lampa 95,0°, panel 5,12 V, 142,35 mA, 728,8 mW, od nulovania 1,534 J.

`pan` a `tilt` sú uhly, ktoré posiela firmvér do serv, nie namerané polohy. Servo spätnú väzbu nemá.

### Priepustnosť

Riadok s najdlhšími možnými hodnotami má najviac 96 bajtov vrátane `\r\n`, bežný riadok okolo 75 bajtov:
- 115200 Bd pri 8N1 prenesie 11 520 B/s, 50 riadkov × 96 B = 4800 B/s, teda linka je vyťažená najviac na **42 %**,
- jeden riadok trvá na linke ~8,3 ms. Keby sa posielal blokujúco, zabral by takmer polovicu 20 ms ticku. Preto sa vysiela cez TX buffer a prerušenie TXE,
- pri 38400 Bd (~3800 B/s) by sa telemetria do linky **nezmestila**.

## 5. Pravidlá zmien

- Nové stĺpce sa pridávajú **iba na koniec**. Poradie ani mierky existujúcich stĺpcov sa nemenia.
- `tools/logger.py` uloží CSV s hlavičkou z riadku `#H` a skripty čítajú stĺpce podľa názvu. Pridaný stĺpec ich preto nerozbije.
- Nové príkazy a stĺpce sa zapíšu sem a do tabuľky v README.

## 6. Otvorené body na dohodu

1. **Kde beží `fsm_tick()`.** Návrh: TIM6 prerušenie iba nastaví príznak a `fsm_tick()` aj `comm_process()` bežia v hlavnej slučke. Potom príkazy z UART môžu meniť parametre regulátora bez ochrany proti súbehu. Ak to schválime, zmeníme vetu v README „regulácia beží v prerušení časovača“.
2. **Király:** sú hodnoty snímačov po kalibrácii v rozsahu 0 … 1? Aký je prah S pre stratu zdroja?
3. **Maszay:** názvy funkcií `control_set_*` a `testrig_*` a kódy regulátorov. Pre meranie skoku chýba v `RIG` príkaz na skok lampy, napríklad `RIG JUMP <deg>`. Pre meranie energie treba profil s pevnou dĺžkou a `testrig_running()`.
4. **Marcinek:** názvy `motion_set_vmax()`, `motion_set_limits()` a funkcia na zadanie cieľovej polohy pre `GOTO`.
5. **Meranie energie:** príkaz `EXP <profil> TRACK|FIXED`, ktorý spustí oba behy za rovnakých podmienok. Doplní sa po dohode o prípravku.
