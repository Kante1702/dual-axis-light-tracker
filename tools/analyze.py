import argparse
import csv
import glob
import math
import os

import numpy as np

SCALE = {"t_ms": 1000.0, "lh": 1000.0, "ph": 1000.0, "ld": 1000.0, "pd": 1000.0,
         "s": 1000.0, "e_h": 1000.0, "e_v": 1000.0, "pan": 10.0, "tilt": 10.0,
         "lamp": 10.0, "u_mv": 1000.0, "i_ua": 1000.0, "p_uw": 1000.0, "e_mj": 1000.0}
CTRL_NAMES = {0: "STEP", 1: "P", 2: "PI"}
RIG_RUN = 3

JUMP_KEYS = [("ts", "ustálenie [s]"), ("over", "prekmit [°]"), ("over_pct", "prekmit [%]"),
             ("cross", "prechody"), ("ess", "ust. chyba [°]"), ("ess_s", "ust. chyba sním. [°]"),
             ("osc_s", "kmity [1/s]"), ("travel", "dráha/lampa")]
TRACK_KEYS = [("rms", "RMS [°]"), ("max", "max [°]"), ("bias", "priemer [°]"),
              ("lag", "oneskorenie [s]"), ("rms_s", "RMS sním. [°]"),
              ("osc_s", "kmity [1/s]"), ("travel", "dráha/lampa")]


def load(path):
    header = None
    rows = []
    with open(path, newline="") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith("#H"):
                header = [h.strip() for h in line[2:].split(",")]
                continue
            if line.startswith("#"):
                continue
            parts = [p.strip() for p in line.split(",")]
            if header is None:
                header = parts
                continue
            try:
                rows.append([float(p) for p in parts])
            except ValueError:
                continue
    n = len(header)
    rows = [r for r in rows if len(r) == n]
    data = np.array(rows, dtype=float)
    cols = {h: data[:, i] / SCALE.get(h, 1.0) for i, h in enumerate(header)}
    cols["t"] = cols.pop("t_ms")
    return cols


def cut_run(c, a):
    t = c["t"]
    if a.all:
        mask = np.ones_like(t, dtype=bool)
    elif a.t0 is not None or a.t1 is not None:
        lo = a.t0 if a.t0 is not None else -np.inf
        hi = a.t1 if a.t1 is not None else np.inf
        mask = (t >= lo) & (t <= hi)
    elif "rig_phase" in c:
        mask = c["rig_phase"] == RIG_RUN
    else:
        mask = np.ones_like(t, dtype=bool)

    idx = np.where(mask)[0]
    if len(idx) < 5:
        return None, None
    breaks = np.where(np.diff(idx) > 1)[0]
    if len(breaks):
        idx = idx[:breaks[0] + 1]

    i0 = idx[0]
    ip = i0 - 1 if i0 > 0 else i0
    run = {k: v[idx] for k, v in c.items()}
    run["t"] = run["t"] - run["t"][0]
    prev = {k: v[ip] for k, v in c.items()}
    return run, prev


def detect_profile(run, prev):
    lamp = run["lamp"]
    if np.mean(np.abs(np.diff(lamp)) < 1e-6) > 0.8:
        return "jump"
    t = run["t"]
    p = np.polyfit(t, lamp, 1)
    if np.std(lamp - np.polyval(p, t)) < 0.5:
        return "ramp"
    return "sine"


def sensor_error(run, a):
    e = run.get("e_h")
    if e is None:
        return None
    es = a.sign * e * a.width
    es[np.abs(e) > 0.9] = np.nan
    return es


def count_crossings(x, h):
    state = 0
    n = 0
    for v in x:
        if v > h:
            s = 1
        elif v < -h:
            s = -1
        else:
            continue
        if state != 0 and s != state:
            n += 1
        state = s
    return n


def moving_mean(x, n):
    if n < 2 or len(x) < n:
        return np.full_like(x, np.mean(x))
    pad = n // 2
    xp = np.pad(x, (pad, n - 1 - pad), mode="edge")
    return np.convolve(xp, np.ones(n) / n, mode="valid")


def motion_metrics(run, prev, a, t_from=0.0):
    t = run["t"]
    dt = float(np.median(np.diff(t)))
    sel = t >= t_from
    err = (run["lamp"] - run["pan"])[sel]
    dur = t[sel][-1] - t[sel][0] if np.count_nonzero(sel) > 1 else 0.0
    osc = err - moving_mean(err, max(2, int(round(a.osc_win / dt))))
    n_osc = count_crossings(osc, a.osc_hyst) / 2.0
    pan_travel = np.sum(np.abs(np.diff(run["pan"]))) + abs(run["pan"][0] - prev["pan"])
    lamp_travel = np.sum(np.abs(np.diff(run["lamp"]))) + abs(run["lamp"][0] - prev["lamp"])
    return {"osc_s": n_osc / dur if dur > 0 else math.nan,
            "travel": pan_travel / lamp_travel if lamp_travel > 0 else math.nan}


def estimate_lag(lamp, pan, dt, max_s=1.0):
    best_k, best = 0, math.inf
    for k in range(0, int(max_s / dt) + 1):
        d = lamp - pan if k == 0 else lamp[:-k] - pan[k:]
        r = float(np.sqrt(np.mean(d ** 2)))
        if r < best:
            best, best_k = r, k
    return best_k * dt if best_k < int(max_s / dt) else math.nan


def jump_metrics(run, prev, a):
    t = run["t"]
    target = run["lamp"][-1]
    amp = target - prev["pan"]
    err = run["lamp"] - run["pan"]

    out = np.where(np.abs(err) > a.band)[0]
    if len(out) == 0:
        ts = 0.0
    elif out[-1] == len(t) - 1:
        ts = math.nan
    else:
        ts = float(t[out[-1] + 1])

    d = 1.0 if amp >= 0 else -1.0
    over = max(0.0, float(np.max((run["pan"] - target) * d)))
    last = t >= t[-1] - a.ess_win
    es = sensor_error(run, a)

    m = {"ts": ts,
         "over": over,
         "over_pct": 100.0 * over / abs(amp) if abs(amp) > 0 else math.nan,
         "cross": count_crossings(err, a.hyst),
         "ess": float(np.mean(err[last])),
         "ess_s": float(np.nanmean(es[last])) if es is not None else math.nan}
    m.update(motion_metrics(run, prev, a, ts if not math.isnan(ts) else 0.0))
    return m


def track_metrics(run, prev, a):
    t = run["t"]
    sel = t >= a.skip
    err = (run["lamp"] - run["pan"])[sel]
    dt = float(np.median(np.diff(t)))
    es = sensor_error(run, a)

    m = {"rms": float(np.sqrt(np.mean(err ** 2))),
         "max": float(np.max(np.abs(err))),
         "bias": float(np.mean(err)),
         "lag": estimate_lag(run["lamp"][sel], run["pan"][sel], dt),
         "rms_s": float(np.sqrt(np.nanmean(es[sel] ** 2))) if es is not None else math.nan}
    m.update(motion_metrics(run, prev, a, a.skip))
    return m


def fmt(vals):
    v = np.array([x for x in vals if not (isinstance(x, float) and math.isnan(x))], dtype=float)
    if len(v) == 0:
        return "-"
    if len(v) == 1:
        return f"{v[0]:.2f}"
    return f"{np.mean(v):.2f}±{np.std(v, ddof=1):.2f}"


def print_table(profile, groups):
    keys = JUMP_KEYS if profile == "jump" else TRACK_KEYS
    print(f"\n=== {profile} ===")
    head = f"{'regulátor':<10}{'n':>3}" + "".join(f"{lbl:>22}" for _, lbl in keys)
    print(head)
    for ctrl in ["STEP", "P", "PI"]:
        runs = groups.get(ctrl)
        if not runs:
            continue
        line = f"{ctrl:<10}{len(runs):>3}"
        for k, _ in keys:
            line += f"{fmt([r['metrics'][k] for r in runs]):>22}"
        print(line)


def write_summary(path, results):
    keys = sorted({k for r in results for k in r["metrics"]})
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["file", "profile", "ctrl"] + keys)
        for r in results:
            w.writerow([r["file"], r["profile"], r["ctrl"]] +
                       [r["metrics"].get(k, "") for k in keys])


def plot(by_profile, a):
    import matplotlib.pyplot as plt
    for profile, groups in by_profile.items():
        fig, axs = plt.subplots(3, 1, sharex=True, figsize=(10, 8))
        first = next(iter(groups.values()))[0]
        axs[0].plot(first["run"]["t"], first["run"]["lamp"], "k--", label="lampa")
        for ctrl in ["STEP", "P", "PI"]:
            runs = groups.get(ctrl)
            if not runs:
                continue
            r = runs[0]["run"]
            axs[0].plot(r["t"], r["pan"], label=ctrl)
            axs[1].plot(r["t"], r["lamp"] - r["pan"], label=ctrl)
            es = sensor_error(r, a)
            if es is not None:
                axs[2].plot(r["t"], es, label=ctrl)
        for ax in axs[1:]:
            ax.axhline(a.band, color="gray", lw=0.5)
            ax.axhline(-a.band, color="gray", lw=0.5)
        axs[0].set_ylabel("uhol [°]")
        axs[1].set_ylabel("chyba z uhlov [°]")
        axs[2].set_ylabel("chyba zo snímačov [°]")
        axs[2].set_xlabel("čas od začiatku RUN [s]")
        axs[0].set_title(profile)
        axs[0].legend()
        fig.tight_layout()
        fig.savefig(os.path.join(a.out, f"analyza_{profile}.png"), dpi=120)
    plt.show()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("--profile", choices=["jump", "ramp", "sine"])
    ap.add_argument("--band", type=float, default=2.0)
    ap.add_argument("--hyst", type=float, default=0.5)
    ap.add_argument("--osc-win", type=float, default=0.5)
    ap.add_argument("--osc-hyst", type=float, default=0.25)
    ap.add_argument("--ess-win", type=float, default=1.0)
    ap.add_argument("--skip", type=float, default=0.0)
    ap.add_argument("--width", type=float, default=15.0)
    ap.add_argument("--sign", type=float, default=1.0)
    ap.add_argument("--t0", type=float)
    ap.add_argument("--t1", type=float)
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--out", default="analyza_out")
    ap.add_argument("--no-plot", action="store_true")
    a = ap.parse_args()

    paths = []
    for pat in a.files:
        hits = glob.glob(pat)
        paths.extend(sorted(hits) if hits else [pat])

    os.makedirs(a.out, exist_ok=True)
    results = []
    by_profile = {}

    for p in paths:
        try:
            cols = load(p)
        except (OSError, StopIteration, IndexError, KeyError, TypeError) as ex:
            print(f"{p}: nedá sa načítať ({ex})")
            continue
        run, prev = cut_run(cols, a)
        if run is None:
            print(f"{p}: nenašla sa fáza RUN")
            continue
        profile = a.profile or detect_profile(run, prev)
        ctrl = CTRL_NAMES.get(int(np.round(np.median(run["ctrl"]))), "?") if "ctrl" in run else "?"
        m = jump_metrics(run, prev, a) if profile == "jump" else track_metrics(run, prev, a)
        if "overruns" in run:
            m["overruns"] = int(run["overruns"][-1] - run["overruns"][0])
            if m["overruns"] > 0:
                print(f"{p}: počas merania {m['overruns']} nestihnutých tickov")
        rec = {"file": os.path.basename(p), "profile": profile, "ctrl": ctrl, "metrics": m, "run": run}
        results.append(rec)
        by_profile.setdefault(profile, {}).setdefault(ctrl, []).append(rec)

    if not results:
        return

    for profile in ["jump", "ramp", "sine"]:
        if profile in by_profile:
            print_table(profile, by_profile[profile])

    write_summary(os.path.join(a.out, "suhrn.csv"), results)
    print(f"\nvýsledky po súboroch: {os.path.join(a.out, 'suhrn.csv')}")

    if not a.no_plot:
        plot(by_profile, a)


if __name__ == "__main__":
    main()