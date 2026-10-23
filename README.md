# Dvojosový sledovač svetla (STM32)

Semestrálny projekt z predmetu VRS, ZS 2026/27.

**Tím:**
- [Meno 1]: mechanika a servá
- [Meno 2]: snímače a kalibrácia
- [Meno 3]: regulácia a testovanie
- [Meno 4]: režimy, komunikácia, meranie energie

## Zadanie

Navrhnite a zostrojte mechanizmus so STM32 a dvoma servomotormi, ktorý automaticky natáča snímač alebo malý solárny panel smerom k svetelnému zdroju. Pomocou štyroch svetelných snímačov oddelených tieniacou prepážkou určujte horizontálnu a vertikálnu odchýlku od zdroja. Implementujte kalibráciu snímačov, filtráciu meraní a reguláciu natočenia v oboch osiach. Zariadenie musí umožňovať režim sledovania aj odvracania od svetla, vyhľadávanie pri strate zdroja a obmedzenie rozsahu a rýchlosti pohybu. Porovnajte jednoduché krokové riadenie s proporcionálnou alebo PI reguláciou podľa doby ustálenia, kmitania a presnosti sledovania pohybujúceho sa zdroja. Voliteľne merajte energiu získanú solárnym panelom a porovnajte ju s pevne natočeným panelom.

## Popis projektu

Sledovač tvorí pan-tilt mechanizmus s dvoma servami, na ktorom je malý solárny panel. Vedľa panelu je snímacia hlava so štyrmi fotorezistormi a medzi nimi prepážka v tvare kríža. Keď svetlo dopadá zboku, prepážka zatieni časť snímačov a z rozdielu ich hodnôt vieme, kam treba hlavu natočiť.

Snímače označujeme podľa polohy pri pohľade spredu:

```
 LH | PH
----+----
 LD | PD
```

Z kalibrovaných a vyfiltrovaných hodnôt počítame:

```
S   = LH + PH + LD + PD
e_h = ((LH + LD) - (PH + PD)) / S
e_v = ((LH + PH) - (LD + PD)) / S
```

Odchýlky delíme súčtom S, aby výsledok nezávisel od sily zdroja. Keď S klesne pod nastavený prah, zariadenie berie zdroj ako stratený a prejde do vyhľadávania.

### Čo má zariadenie vedieť

- čítať 4 snímače cez ADC s DMA, vzorkovanie spúšťa časovač (1 kHz)
- kalibrovať každý snímač (tma a referenčné svetlo) a uložiť koeficienty do Flash
- filtrovať merania: priemer cez 20 ms (odfiltruje blikanie LED lámp na 100 Hz) a EMA filter
- regulovať natočenie v oboch osiach, regulačná slučka beží každých 20 ms
- režimy sledovanie, odvracanie, vyhľadávanie, manuál a kalibrácia
- obmedziť rozsah (napr. pan 10-170°, tilt 0-90°) a maximálnu rýchlosť v °/s
- prepínať krokový, P a PI regulátor cez UART bez nového nahrávania programu
- posielať telemetriu do PC
- voliteľne merať energiu z panelu

Chceme dosiahnuť ustálenú chybu do ±2° pri statickom zdroji a nájsť stratený zdroj do 10 s.

### Odvracanie od svetla

Regulátor dostane odchýlku s opačným znamienkom, takže hlava sa točí od zdroja. Zastaví sa na hranici rozsahu alebo keď S klesne pod prah.

### Vyhľadávanie

Ak S zostane pod prahom dlhšie ako 0,5 s, hlava prejde špirálu okolo poslednej známej polohy zdroja. Keď tam zdroj nenájde, prejde celý rozsah po riadkoch, zapamätá si polohu s najväčším S, vráti sa na ňu a pokračuje v sledovaní.

### Regulácia

Servo nastavujeme na uhol, snímače nám ale dajú iba to, o koľko je zdroj mimo stredu. Regulátor preto v každej perióde (T = 20 ms) pripočíta k aktuálnemu uhlu prírastok:

- krokové riadenie: `θ += Δ * sign(e)`, ak |e| prekročí pásmo necitlivosti
- P: `θ += Kp * e`
- PI: `θ += Kp * e + Ki * sum(e)`, s anti-windupom pri narazení na limit

Výstup ešte prejde obmedzením uhla a maximálneho prírastku za periódu.

Pri krokovom riadení s veľkým Δ čakáme kmitanie okolo cieľa. P regulátor by mal za zdrojom, ktorý ide konštantnou rýchlosťou, stále trochu zaostávať a PI by to zaostávanie mal odstrániť. Meraním to chceme overiť.

Odchýlka e je lineárna iba v malom okolí cieľa, pri väčšom uhle sa saturuje na ±1. Šírku tejto oblasti určuje výška prepážky, preto charakteristiku e(φ) zmeriame a podľa nej nastavíme zosilnenia.

### Porovnanie regulátorov

Na meranie si postavíme prípravok: lampu na treťom serve, ktoré ovláda ten istý STM32. Uhol lampy tak poznáme v každom okamihu a chybu sledovania vieme vypočítať.

Meriame dva prípady:
1. Skok: lampa sa rýchlo presunie do novej polohy. Zaujíma nás doba ustálenia do ±2°, prekmit, počet kmitov a ustálená chyba.
2. Pohyb: lampa ide rampou s konštantnou rýchlosťou alebo sínusom. Zaujíma nás RMS chyba, maximálna chyba a oneskorenie.

Každé meranie spravíme aspoň 3x pre každý regulátor. STM32 posiela dáta cez UART, v PC ich ukladáme do CSV a vyhodnocujeme v Pythone.

### Meranie energie (voliteľné)

Použijeme dva rovnaké panely, jeden na sledovači a druhý pevný. Každý má vlastný INA219 a rovnaký záťažový rezistor. Počas rovnakého scenára integrujeme výkon oboch panelov a porovnáme energiu. Ak stihneme, odmeriame aj spotrebu serv, aby bolo vidieť, či sa sledovanie oplatí.

## Hardvér

- NUCLEO-F303K8
- 3x servo MG90S alebo SG90 (2 na sledovač, 1 na prípravok s lampou)
- pan-tilt držiak (3D tlač)
- 4x fotorezistor GL5528 a 4x rezistor 10 kΩ do deličov
- tieniaca prepážka (3D tlač)
- 2x solárny panel 5-6 V, cca 1 W
- 2x modul INA219 (adresy 0x40 a 0x41)
- 2x záťažový rezistor pre panely
- externý zdroj 5 V / min. 2 A pre servá, so spoločnou zemou s Nucleo
- kondenzátor 470-1000 µF na napájanie serv
- 2x LED lampa
- tlačidlo, kontaktné pole, vodiče

### Zapojenie (návrh, overíme v CubeMX)

| Funkcia | Periféria | Pin | Nucleo |
|---|---|---|---|
| LDR LH | ADC1_IN1 | PA0 | A0 |
| LDR PH | ADC1_IN2 | PA1 | A1 |
| LDR LD | ADC1_IN4 | PA3 | A2 |
| LDR PD | ADC1_IN11 | PB0 | D3 |
| servo pan | TIM3_CH1 | PB4 | D12 |
| servo tilt | TIM3_CH2 | PB5 | D11 |
| servo lampy | TIM3_CH4 | PB1 | D6 |
| UART do PC | USART2 | PA2 / PA15 | VCP |
| I2C (INA219) | I2C1 SCL / SDA | PB6 / PB7 | D5 / D4 |
| tlačidlo | EXTI | PA12 | D2 |
| LED | GPIO | PB3 | D13 |

A4 a A5 nepoužívame, na Nucleo-32 ich SB16/SB18 spájajú s D4/D5, kde máme I2C.

Servá riadime PWM 50 Hz s impulzom 0,5-2,5 ms. Časovač beží na 64 MHz, PSC = 63 a ARR = 19999, takže 1 tik = 1 µs.

## Softvér

- STM32CubeIDE a CubeMX, knižnica LL ako na cvičeniach
- C na STM32, Python (pyserial, numpy, matplotlib) na PC
- ADC spúšťa časovač a dáta ukladá DMA, regulácia beží v prerušení časovača každých 20 ms, UART príkazy spracúvame v hlavnej slučke

UART príkazy (115200 Bd):

```
MODE TRACK | AVOID | SEARCH | MANUAL | CALIB
CTRL STEP | P | PI
KP <x>  KI <x>  STEP <deg>  VMAX <deg/s>
LIM PAN <min> <max>  LIM TILT <min> <max>
GOTO <pan> <tilt>
CAL DARK | BRIGHT | SAVE
RIG RAMP <deg/s> | SINE <amp> <perioda> | STOP
LOG ON | OFF
```

Telemetria ide ako CSV riadok 50x za sekundu: čas, 4 snímače, S, e_h, e_v, uhly pan a tilt, režim, regulátor, uhol lampy, výkony panelov.

## Rozdelenie úloh

### [Meno 1]: mechanika a servá
- návrh a stavba pan-tilt mechanizmu, uchytenie panelu a snímacej hlavy
- napájanie serv a schéma celého zapojenia
- PWM na TIM3 a ovládač serv (`servo.c`), kalibrácia uhla pre každé servo
- obmedzenie rozsahu a rýchlosti pohybu (`motion.c`)
- meranie skutočnej rýchlosti a mŕtveho pásma serv

### [Meno 2]: snímače, filtrácia, kalibrácia
- zapojenie fotorezistorov a výber rezistorov do deličov
- návrh prepážky, porovnanie dvoch výšok
- ADC1 so 4 kanálmi, spúšťanie časovačom, DMA (`sensors.c`)
- filtre (`filter.c`) a kalibrácia s uložením do Flash (`calib.c`)
- výpočet e_h, e_v, S a detekcia straty zdroja
- meranie charakteristiky e(φ) a šumu pred filtráciou a po nej

### [Meno 3]: regulácia a vyhodnotenie
- krokový, P a PI regulátor pre obe osi, anti-windup (`control.c`)
- časovanie regulačnej slučky
- prípravok s lampou na serve a pohybové profily (`testrig.c`)
- nastavenie Kp, Ki a Δ
- Python skript na metriky a grafy (`tools/analyze.py`)
- porovnanie regulátorov

### [Meno 4]: režimy, komunikácia, energia
- stavový automat režimov, prepínanie tlačidlom a cez UART, LED indikácia (`fsm.c`)
- odvracanie od svetla a vyhľadávanie (`search.c`)
- UART s prerušením a kruhovým bufferom, príkazy a telemetria (`comm.c`)
- ukladanie telemetrie z COM portu do CSV (`tools/logger.py`)
- voliteľne INA219 cez I2C a porovnanie energie s pevným panelom (`ina219.c`)
- meranie času nájdenia zdroja z rôznych polôh

`main.c` a spájanie modulov robíme spolu. Každý popíše svoju časť v `docs/`.

## Harmonogram

1. schválenie špecifikácie, nákup súčiastok, repozitár a CubeMX projekt
2. hotový mechanizmus, servá idú z PWM, ADC číta všetky 4 snímače
3. filtre a kalibrácia, P regulácia v oboch osiach pri statickom zdroji
4. režimy, vyhľadávanie, UART
5. prípravok, krokové a PI riadenie, merania
6. meranie energie (ak stihneme), dokončenie a príprava ukážky

## Štruktúra repozitára

```
firmware/    projekt STM32CubeIDE
hardware/    schéma, pinout, fotky zapojenia
mechanics/   3D modely držiaka, prepážky a prípravku
tools/       Python skripty
docs/        merania, grafy, popis častí
```

## Práca s Gitom

Do `main` dávame iba kód, ktorý ide skompilovať. Každý pracuje vo vlastnej vetve `feature/...` a do `main` mergujeme cez pull request, ktorý pozrie aspoň jeden ďalší člen tímu.
