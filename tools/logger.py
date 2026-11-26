#!/usr/bin/env python3
"""
logger.py - zaznam telemetrie zo sledovaca svetla do CSV
Protokol je v docs/protokol_uart.md.

Pouzitie:
  python logger.py --list                       zoznam COM portov
  python logger.py --port COM5                  zaznam z dosky
  python logger.py --port COM5 --name skok_P    nazov merania v nazve suboru
  python logger.py --fake                       test bez dosky (simulovane STM32)

Prikazy sa pisu priamo do konzoly (napr. LOG ON, KP 0.8).
Koniec: quit alebo Ctrl+C.
Kazde LOG ON (riadok #H) zacne novy CSV subor v priecinku tools/data.
"""

import argparse
import datetime
import math
import pathlib
import re
import sys
import threading
import time

DEFAULT_BAUD = 115200
DATA_DIR = pathlib.Path(__file__).resolve().parent / "data"
STATUS_PERIOD_S = 5.0


class Recorder:
    """Spracuje riadky zo STM32: #H -> novy CSV, cisla -> riadok CSV, # -> konzola."""

    def __init__(self, out_dir, name=""):
        self.out_dir = pathlib.Path(out_dir)
        self.name = name
        self.file = None
        self.path = None
        self.columns = 0
        self.rows = 0
        self.bad = 0
        self.no_header = 0
        self.last_status = time.monotonic()

    def handle(self, line):
        line = line.strip()
        if not line:
            return
        if line.startswith("#H "):
            self._start(line[3:].strip())
        elif line.startswith("#"):
            print(f"  {line}")
        elif line[0].isdigit():
            self._row(line)
        else:
            print(f"  ? {line}")
        self._status()

    def _start(self, header):
        self.close()
        self.out_dir.mkdir(parents=True, exist_ok=True)
        stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        fname = f"{stamp}_{self.name}.csv" if self.name else f"{stamp}.csv"
        self.path = self.out_dir / fname
        self.file = open(self.path, "w", newline="", encoding="ascii")
        self.file.write(header + "\n")
        self.columns = header.count(",") + 1
        self.rows = 0
        self.bad = 0
        print(f"  zaznam do {self.path} ({self.columns} stlpcov)")

    def _row(self, line):
        if self.file is None:
            self.no_header += 1     # telemetria bez #H, napr. logger zapnuty az po LOG ON
            return
        parts = line.split(",")
        if len(parts) != self.columns or not all(p.lstrip("-").isdigit() for p in parts):
            self.bad += 1           # poskodeny riadok, nezapise sa
            return
        self.file.write(line + "\n")
        self.rows += 1

    def _status(self):
        now = time.monotonic()
        if now - self.last_status < STATUS_PERIOD_S:
            return
        self.last_status = now
        if self.file is not None:
            self.file.flush()
            print(f"  ... {self.rows} riadkov, chybnych {self.bad}")
        elif self.no_header:
            print(f"  ... prichadza telemetria bez hlavicky ({self.no_header} riadkov), posli LOG ON")
            self.no_header = 0

    def close(self):
        if self.file is None:
            return
        self.file.close()
        self.file = None
        print(f"  ulozene {self.rows} riadkov, chybnych {self.bad}: {self.path}")


def reader(port, rec, stop):
    """Bezi vo vlakne: cita bajty z portu, sklada riadky a posiela ich do Recorder."""
    buf = bytearray()
    while not stop.is_set():
        try:
            chunk = port.read(port.in_waiting or 1)
        except Exception as e:      # odpojena doska a pod.
            print(f"Chyba portu: {e}")
            stop.set()
            break
        if not chunk:
            continue
        buf += chunk
        while True:
            i = buf.find(b"\n")
            if i < 0:
                break
            line = bytes(buf[:i])
            del buf[:i + 1]
            rec.handle(line.decode("ascii", errors="replace"))
        if len(buf) > 4096:         # dlho bez \n, nieco je zle
            buf.clear()


class FakeStm32:
    """Nahrada COM portu: odpoveda na prikazy a posiela telemetriu ako STM32."""

    HEADER = ("t_ms,lh,ph,ld,pd,s,e_h,e_v,pan,tilt,mode,ctrl,lamp,"
              "u_mv,i_ua,p_uw,e_mj,rig_phase,overruns")
    MODES = ["TRACK", "AVOID", "SEARCH", "MANUAL", "CALIB"]

    def __init__(self):
        self.out = bytearray()
        self.lock = threading.Lock()
        self.log_on = False
        self.mode = 0
        self.t_ms = 0
        self.e_mj = 0.0
        self.running = True
        threading.Thread(target=self._tick, daemon=True).start()

    @property
    def in_waiting(self):
        with self.lock:
            return len(self.out)

    def read(self, n):
        with self.lock:
            data = bytes(self.out[:n])
            del self.out[:n]
        if not data:
            time.sleep(0.01)
        return data

    def write(self, data):
        for line in data.decode("ascii", errors="replace").replace("\r", "\n").split("\n"):
            words = line.upper().split()
            if words:
                self._command(words)
        return len(data)

    def close(self):
        self.running = False

    def _send(self, text):
        with self.lock:
            self.out += (text + "\r\n").encode("ascii")

    def _command(self, w):
        if w == ["PING"]:
            self._send("#OK PING")
        elif w == ["LOG", "ON"]:
            self.log_on = True
            self._send("#OK LOG")
            self._send("#H " + self.HEADER)
        elif w == ["LOG", "OFF"]:
            self.log_on = False
            self._send("#OK LOG")
        elif len(w) == 2 and w[0] == "MODE" and w[1] in self.MODES:
            self.mode = self.MODES.index(w[1])
            self._send("#OK MODE")
            self._send(f"#EV MODE {w[1]}")
        elif w == ["PV", "RESET"]:
            self.e_mj = 0.0
            self._send("#OK PV")
        else:
            self._send(f"#ERR NOT_IMPL {w[0]}")

    def _tick(self):
        while self.running:
            time.sleep(0.02)
            self.t_ms += 20
            if not self.log_on:
                continue
            t = self.t_ms / 1000.0
            e_h = 0.3 * math.sin(2 * math.pi * t / 4)
            e_v = 0.1 * math.cos(2 * math.pi * t / 4)
            ldr = [500 + 150 * e_h + 150 * e_v, 500 - 150 * e_h + 150 * e_v,
                   500 + 150 * e_h - 150 * e_v, 500 - 150 * e_h - 150 * e_v]
            p_uw = 600000 + 100000 * math.cos(2 * math.pi * t / 4)
            self.e_mj += p_uw * 0.02 / 1000.0
            row = [self.t_ms, *[round(v) for v in ldr], round(sum(ldr)),
                   round(e_h * 1000), round(e_v * 1000), 900, 450, self.mode, 2,
                   round(900 + 300 * math.sin(2 * math.pi * t / 4)),
                   5100, round(p_uw / 5.1), round(p_uw), round(self.e_mj), 0, 0]
            self._send(",".join(str(v) for v in row))


def list_ports():
    try:
        from serial.tools import list_ports as lp
    except ImportError:
        sys.exit("Chyba pyserial: pip install pyserial")
    ports = list(lp.comports())
    if not ports:
        print("Ziadny COM port. Je Nucleo pripojene?")
    for p in ports:
        print(f"{p.device:8} {p.description}")


def open_port(name, baud):
    try:
        import serial
    except ImportError:
        sys.exit("Chyba pyserial: pip install pyserial")
    try:
        return serial.Serial(name, baud, timeout=0.1)
    except serial.SerialException as e:
        sys.exit(f"Port {name} sa neda otvorit: {e}\n"
                 "Skontroluj --list a ci port nema otvoreny iny program (PuTTY, CubeIDE terminal).")


def main():
    ap = argparse.ArgumentParser(description="Zaznam telemetrie zo sledovaca svetla do CSV.")
    ap.add_argument("--port", help="COM port, napr. COM5")
    ap.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    ap.add_argument("--name", default="", help="nazov merania, prida sa do nazvu suboru")
    ap.add_argument("--out", default=str(DATA_DIR), help="priecinok pre CSV")
    ap.add_argument("--list", action="store_true", help="vypise COM porty")
    ap.add_argument("--fake", action="store_true", help="simulovane STM32, bez dosky")
    args = ap.parse_args()

    if args.list:
        list_ports()
        return
    if not args.fake and not args.port:
        ap.error("zadaj --port COMx alebo --fake")
    if not re.fullmatch(r"[A-Za-z0-9_-]*", args.name):
        ap.error("--name moze obsahovat iba pismena bez diakritiky, cisla, _ a -")

    port = FakeStm32() if args.fake else open_port(args.port, args.baud)
    rec = Recorder(args.out, args.name)
    stop = threading.Event()
    th = threading.Thread(target=reader, args=(port, rec, stop), daemon=True)
    th.start()

    print(f"Pripojene k {'simulovanemu STM32' if args.fake else args.port}. "
          "Pis prikazy (napr. PING, LOG ON), koniec: quit alebo Ctrl+C.")
    try:
        while not stop.is_set():
            cmd = input().strip()
            if not cmd:
                continue
            if cmd.lower() in ("quit", "exit", "q"):
                break
            port.write((cmd + "\n").encode("ascii", errors="replace"))
    except (KeyboardInterrupt, EOFError):
        pass
    finally:
        stop.set()
        th.join(timeout=1.0)
        port.close()
        rec.close()


if __name__ == "__main__":
    main()