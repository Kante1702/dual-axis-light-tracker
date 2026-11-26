import argparse
import csv
import math
import os
import random

import numpy as np

T = 0.020
SUB = 20
DT = T / SUB

CTRL_CODES = {"STEP": 0, "P": 1, "PI": 2}

COLUMNS = ["t_ms", "lh", "ph", "ld", "pd", "s", "e_h", "e_v", "pan", "tilt",
           "mode", "ctrl", "lamp", "u_mv", "i_ua", "p_uw", "e_mj", "rig_phase"]

RIG_IDLE, RIG_GOTO_START, RIG_SETTLE, RIG_RUN, RIG_DONE = range(5)


class Plant:
    def __init__(self, a):
        self.width = a.width
        self.noise = a.noise

    def sensor(self, lamp, pan):
        e = (lamp - pan) / self.width
        e = max(-1.0, min(1.0, e))
        return e + random.gauss(0.0, self.noise)


class Servo:
    def __init__(self, pos, a):
        self.pos = pos
        self.vel_max = a.servo_speed
        self.tau = a.servo_tau
        self.db = a.servo_db

    def step(self, cmd):
        diff = cmd - self.pos
        if abs(diff) <= self.db:
            return self.pos
        v = diff / self.tau
        v = max(-self.vel_max, min(self.vel_max, v))
        self.pos += v * DT
        return self.pos


class Controller:
    def __init__(self, kind, kp, ki, step, db):
        self.kind = kind
        self.kp = kp
        self.ki = ki
        self.step_deg = step
        self.db = db
        self.sum = 0.0
        self.e_last = 0.0
        self.d_req = 0.0
        self.integrated = False

    def update(self, e, sat=0):
        if self.kind == "STEP":
            d = self.step_deg if e > self.db else (-self.step_deg if e < -self.db else 0.0)
        elif self.kind == "P":
            d = self.kp * e
        else:
            if sat * e <= 0.0:
                self.sum = max(-500.0, min(500.0, self.sum + e))
            d = self.kp * e + self.ki * self.sum
        return d

class Motion:
    def __init__(self, pos, lo, hi, vmax):
        self.pos = pos
        self.lo = lo
        self.hi = hi
        self.dmax = vmax * T
        self.sat = 0

    def move(self, d):
        dc = max(-self.dmax, min(self.dmax, d))
        new = max(self.lo, min(self.hi, self.pos + dc))
        applied = new - self.pos
        self.pos = new
        if applied < d - 1e-6:
            self.sat = 1
        elif applied > d + 1e-6:
            self.sat = -1
        else:
            self.sat = 0
        return applied


class TestRig:
    GOTO_DPS = 60.0
    SETTLE_S = 2.0

    def __init__(self, prof):
        self.p = prof
        self.phase = RIG_GOTO_START
        self.angle = 90.0
        self.ticks = 0
        if prof["type"] == "jump":
            self.duration = prof["hold"]
        elif prof["type"] == "ramp":
            self.duration = abs(prof["end"] - prof["start"]) / prof["speed"]
        else:
            self.duration = prof["period"] * prof["cycles"]

    def profile_angle(self, t):
        p = self.p
        if p["type"] == "jump":
            return p["end"]
        if p["type"] == "ramp":
            d = 1.0 if p["end"] >= p["start"] else -1.0
            return p["start"] + d * p["speed"] * t
        return p["start"] + p["amp"] * math.sin(2.0 * math.pi * t / p["period"])

    def profile_end(self):
        return self.p["start"] if self.p["type"] == "sine" else self.p["end"]

    def step(self):
        if self.phase == RIG_GOTO_START:
            md = self.GOTO_DPS * T
            diff = self.p["start"] - self.angle
            if abs(diff) <= md:
                self.angle = self.p["start"]
                self.ticks = 0
                self.phase = RIG_SETTLE
            else:
                self.angle += md if diff > 0 else -md
        elif self.phase == RIG_SETTLE:
            self.ticks += 1
            if self.ticks * T >= self.SETTLE_S:
                self.ticks = 0
                self.phase = RIG_RUN
        elif self.phase == RIG_RUN:
            t = self.ticks * T
            if t >= self.duration:
                self.angle = self.profile_end()
                self.phase = RIG_DONE
            else:
                self.angle = self.profile_angle(t)
                self.ticks += 1


def make_profile(kind, a):
    if kind == "jump":
        return {"type": "jump", "start": 90.0, "end": a.target, "hold": a.hold}
    if kind == "ramp":
        return {"type": "ramp", "start": 30.0, "end": 150.0, "speed": a.speed}
    return {"type": "sine", "start": 90.0, "amp": a.amp, "period": a.period, "cycles": 3}


def simulate(ctrl_kind, prof, a, kp=None, ki=None, step=None, seed=0):
    random.seed(seed)
    plant = Plant(a)
    ctl = Controller(ctrl_kind,
                     a.kp if kp is None else kp,
                     a.ki if ki is None else ki,
                     a.step if step is None else step,
                     a.db)
    rig = TestRig(prof)
    motion = Motion(90.0, a.pan_min, a.pan_max, a.vmax)
    pan_servo = Servo(90.0, a)
    lamp_servo = Servo(90.0, a)

    e_filt = 0.0
    e_block = 0.0
    rows = []
    tick = 0
    tail = int(a.tail / T)
    done_ticks = None

    while True:
        acc = 0.0
        for _ in range(SUB):
            lamp_pos = lamp_servo.step(rig.angle)
            pan_pos = pan_servo.step(motion.pos)
            acc += plant.sensor(lamp_pos, pan_pos)
        e_block = acc / SUB
        e_filt = a.ema * e_block + (1.0 - a.ema) * e_filt

        d = ctl.update(e_filt, motion.sat)
        motion.move(d)
        rig.step()

        s = a.s_level
        left = s * (1.0 + e_filt) / 2.0
        right = s * (1.0 - e_filt) / 2.0
        rows.append([
            int(round(tick * T * 1000)),
            int(round(left / 2 * 1000)), int(round(right / 2 * 1000)),
            int(round(left / 2 * 1000)), int(round(right / 2 * 1000)),
            int(round(s * 1000)),
            int(round(e_filt * 1000)), 0,
            int(round(motion.pos * 10)), 450,
            0, CTRL_CODES[ctrl_kind],
            int(round(rig.angle * 10)),
            0, 0, 0, 0,
            rig.phase,
        ])

        tick += 1
        if rig.phase == RIG_DONE:
            if done_ticks is None:
                done_ticks = tick
            elif tick - done_ticks >= tail:
                break
        if tick > 100000:
            break
    return rows


def write_csv(path, rows):
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(COLUMNS)
        w.writerows(rows)


def quick_metrics(rows, prof, band=2.0):
    r = np.array(rows, dtype=float)
    run = r[r[:, 17] >= RIG_RUN]
    t = (run[:, 0] - run[0, 0]) / 1000.0
    err = run[:, 12] / 10.0 - run[:, 8] / 10.0
    if prof["type"] == "jump":
        outside = np.where(np.abs(err) > band)[0]
        ts = t[outside[-1] + 1] if len(outside) and outside[-1] + 1 < len(t) else (0.0 if not len(outside) else math.inf)
        step_size = prof["end"] - prof["start"]
        over = max(0.0, -np.min(err * np.sign(step_size)))
        return {"ts": ts, "over": over, "ess": float(np.mean(np.abs(err[-25:])))}
    in_run = run[:, 17] == RIG_RUN
    e = err[in_run]
    return {"rms": float(np.sqrt(np.mean(e ** 2))), "max": float(np.max(np.abs(e)))}


def sweep(a):
    jump = make_profile("jump", a)
    ramp = make_profile("ramp", a)
    print(f"skok {jump['start']:.0f} -> {jump['end']:.0f} deg, rampa {ramp['speed']:.0f} deg/s\n")

    print("STEP")
    print(f"{'delta':>6} {'ts[s]':>7} {'prekmit':>8} {'ess':>6} {'rms':>6} {'max':>6}")
    for st in [0.2, 0.5, 1.0, 1.5, 2.0, 3.0]:
        mj = quick_metrics(simulate("STEP", jump, a, step=st), jump)
        mr = quick_metrics(simulate("STEP", ramp, a, step=st), ramp)
        print(f"{st:6.2f} {mj['ts']:7.2f} {mj['over']:8.2f} {mj['ess']:6.2f} {mr['rms']:6.2f} {mr['max']:6.2f}")

    print("\nP")
    print(f"{'kp':>6} {'ts[s]':>7} {'prekmit':>8} {'ess':>6} {'rms':>6} {'max':>6}")
    for kp in [1, 2, 3, 4, 6, 8, 10, 12, 15]:
        mj = quick_metrics(simulate("P", jump, a, kp=kp), jump)
        mr = quick_metrics(simulate("P", ramp, a, kp=kp), ramp)
        print(f"{kp:6.1f} {mj['ts']:7.2f} {mj['over']:8.2f} {mj['ess']:6.2f} {mr['rms']:6.2f} {mr['max']:6.2f}")

    print("\nPI")
    print(f"{'kp':>6} {'ki':>6} {'ts[s]':>7} {'prekmit':>8} {'ess':>6} {'rms':>6} {'max':>6}")
    for kp in [2, 4, 6, 8]:
        for ki in [0.02, 0.05, 0.1, 0.2, 0.4]:
            mj = quick_metrics(simulate("PI", jump, a, kp=kp, ki=ki), jump)
            mr = quick_metrics(simulate("PI", ramp, a, kp=kp, ki=ki), ramp)
            print(f"{kp:6.1f} {ki:6.2f} {mj['ts']:7.2f} {mj['over']:8.2f} {mj['ess']:6.2f} {mr['rms']:6.2f} {mr['max']:6.2f}")


def plot_runs(results, out):
    import matplotlib.pyplot as plt
    for prof_name, runs in results.items():
        fig, (ax1, ax2) = plt.subplots(2, 1, sharex=True, figsize=(9, 6))
        r0 = np.array(runs[0][1], dtype=float)
        ax1.plot(r0[:, 0] / 1000, r0[:, 12] / 10, "k--", label="lampa")
        for kind, rows in runs:
            r = np.array(rows, dtype=float)
            t = r[:, 0] / 1000
            ax1.plot(t, r[:, 8] / 10, label=kind)
            ax2.plot(t, r[:, 12] / 10 - r[:, 8] / 10, label=kind)
        ax2.axhline(2, color="gray", lw=0.5)
        ax2.axhline(-2, color="gray", lw=0.5)
        ax1.set_ylabel("uhol [°]")
        ax2.set_ylabel("chyba [°]")
        ax2.set_xlabel("čas [s]")
        ax1.legend()
        ax1.set_title(prof_name)
        fig.tight_layout()
        fig.savefig(os.path.join(out, f"sim_{prof_name}.png"), dpi=120)
    plt.show()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ctrl", default="all", choices=["STEP", "P", "PI", "all"])
    ap.add_argument("--profile", default="all", choices=["jump", "ramp", "sine", "all"])
    ap.add_argument("--kp", type=float, default=4.0)
    ap.add_argument("--ki", type=float, default=0.1)
    ap.add_argument("--step", type=float, default=1.0)
    ap.add_argument("--db", type=float, default=0.02)
    ap.add_argument("--target", type=float, default=110.0)
    ap.add_argument("--hold", type=float, default=5.0)
    ap.add_argument("--speed", type=float, default=15.0)
    ap.add_argument("--amp", type=float, default=30.0)
    ap.add_argument("--period", type=float, default=10.0)
    ap.add_argument("--width", type=float, default=15.0)
    ap.add_argument("--noise", type=float, default=0.005)
    ap.add_argument("--ema", type=float, default=0.5)
    ap.add_argument("--s-level", type=float, default=2.0)
    ap.add_argument("--servo-speed", type=float, default=500.0)
    ap.add_argument("--servo-tau", type=float, default=0.05)
    ap.add_argument("--servo-db", type=float, default=0.5)
    ap.add_argument("--vmax", type=float, default=90.0)
    ap.add_argument("--pan-min", type=float, default=10.0)
    ap.add_argument("--pan-max", type=float, default=170.0)
    ap.add_argument("--tail", type=float, default=1.0)
    ap.add_argument("--sweep", action="store_true")
    ap.add_argument("--no-plot", action="store_true")
    ap.add_argument("--out", default="sim_out")
    a = ap.parse_args()

    if a.sweep:
        sweep(a)
        return

    os.makedirs(a.out, exist_ok=True)
    kinds = ["STEP", "P", "PI"] if a.ctrl == "all" else [a.ctrl]
    profs = ["jump", "ramp", "sine"] if a.profile == "all" else [a.profile]

    results = {}
    for pn in profs:
        prof = make_profile(pn, a)
        results[pn] = []
        for k in kinds:
            rows = simulate(k, prof, a)
            write_csv(os.path.join(a.out, f"sim_{pn}_{k}.csv"), rows)
            results[pn].append((k, rows))
            m = quick_metrics(rows, prof)
            print(pn, k, {key: round(v, 2) for key, v in m.items()})

    if not a.no_plot:
        plot_runs(results, a.out)


if __name__ == "__main__":
    main()