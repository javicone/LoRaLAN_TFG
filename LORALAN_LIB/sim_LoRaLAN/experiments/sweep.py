#!/usr/bin/env python3
"""Barridos de los experimentos y estadistica entre replicas.

    python experiments/sweep.py --seeds 30 --out results/perf

Cada configuracion se repite con semillas distintas. Las tasas se dan con intervalo
de Wilson sobre el total de eventos; las latencias, con la media entre replicas de
la mediana y de los percentiles, y su intervalo t de Student.
"""
import argparse
import csv
import hashlib
import json
import math
import statistics
import subprocess
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
EXE = ROOT / "build" / "sim_perf.exe"
EXE254 = ROOT / "build" / "sim_perf254.exe"
LIB = ROOT.parent / "LoRaLAN" / "src"
LOSSES = [0, 10, 20, 35, 50]
T_95 = {4: 2.776, 9: 2.262, 19: 2.093, 29: 2.045}


def configurations():
    for loss in LOSSES:
        yield {"exp": "VAL", "loss": loss}
        yield {"exp": "EXP1E", "loss": loss}
        yield {"exp": "EXP1P", "loss": loss, "nodes": 4}
    # Red de 254 nodos con la configuracion del despliegue.
    yield {"exp": "EXP5", "nodes": 254, "rate": 60, "blind": 0, "poll": 5000, "max_seeds": 5}
    # Ciclo de trabajo con fichajes y retransmisiones.
    for loss in (10, 20):
        yield {"exp": "EXP5", "nodes": 8, "rate": 60, "blind": 0, "poll": 5000, "loss": loss}
    for nodes in (8, 32):
        for rate in (6, 60):
            for blind in (0, 2, 5):
                yield {"exp": "EXP4", "nodes": nodes, "rate": rate, "blind": blind, "poll": 0}
            for blind in (0, 2):
                yield {"exp": "EXP5", "nodes": nodes, "rate": rate, "blind": blind, "poll": 5000}
    for poll in (2000, 10000):
        yield {"exp": "EXP5", "nodes": 8, "rate": 60, "blind": 0, "poll": poll}


def run(config, seed):
    args = [str(EXE254 if config.get("nodes", 0) > 32 else EXE), "--seed", str(seed)]
    for key, value in config.items():
        if key != "max_seeds":
            args += [f"--{key}", str(value)]
    out = subprocess.run(args, capture_output=True, text=True, check=True).stdout
    row = json.loads(out)
    row["config"] = config
    return row


def wilson(k, n, z=1.96):
    if n == 0:
        return (float("nan"), float("nan"))
    p = k / n
    den = 1 + z * z / n
    mid = (p + z * z / (2 * n)) / den
    half = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / den
    return (mid - half, mid + half)


def mean_ci(values):
    values = [v for v in values if v >= 0]
    if not values:
        return (float("nan"), float("nan"), 0)
    mean = statistics.fmean(values)
    if len(values) < 2:
        return (mean, 0.0, len(values))
    t = T_95.get(len(values) - 1, 2.0)
    return (mean, t * statistics.stdev(values) / math.sqrt(len(values)), len(values))


def summarize(rows):
    keys = sorted({k for r in rows for k in r if k not in ("exp", "seed", "config")})
    out = dict(rows[0]["config"])
    out["replicas"] = len(rows)
    out["join_fail"] = sum(r.get("join_fail", 0) for r in rows)
    for key in keys:
        if key == "join_fail":
            continue
        values = [r[key] for r in rows if key in r]
        if key.startswith(("lat_", "gap_", "duty_", "turns_")) or key.endswith("_ms"):
            mean, half, n = mean_ci(values)
            out[key] = round(mean, 3)
            out[key + "_ic"] = round(half, 3)
            out[key + "_n"] = n
        else:
            out[key] = sum(values)
    for num in ("delivered", "confirmed"):
        if num in out and out.get("offered"):
            low, high = wilson(out[num], out["offered"])
            out[num + "_pct"] = round(100 * out[num] / out["offered"], 3)
            out[num + "_ic_lo"] = round(100 * low, 3)
            out[num + "_ic_hi"] = round(100 * high, 3)
    return out


def metadata(seeds):
    digest = hashlib.sha256()
    for path in sorted(LIB.iterdir()):
        digest.update(path.name.encode())
        digest.update(path.read_bytes())

    def git(*args):
        return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True).stdout.strip()

    return {
        "fecha_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "semillas": seeds,
        "commit": git("rev-parse", "--short", "HEAD"),
        "biblioteca_con_cambios_sin_commit": bool(git("status", "--porcelain", "--", str(LIB))),
        "sha256_fuentes_biblioteca": digest.hexdigest(),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--seeds", type=int, default=30)
    parser.add_argument("--out", default=str(ROOT / "results" / "perf"))
    parser.add_argument("--only", default="", help="experimentos separados por comas")
    parser.add_argument("--workers", type=int, default=10)
    args = parser.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    only = set(filter(None, args.only.split(",")))
    configs = [c for c in configurations() if not only or c["exp"] in only]
    jobs = [(c, s) for c in configs for s in range(1, min(args.seeds, c.get("max_seeds", args.seeds)) + 1)]
    print(f"{len(configs)} configuraciones, {len(jobs)} replicas")
    with ThreadPoolExecutor(args.workers) as pool:
        rows = list(pool.map(lambda job: run(*job), jobs))

    with open(out / "raw.jsonl", "w", encoding="utf8", newline="\n") as f:
        for row in rows:
            f.write(json.dumps(row, sort_keys=True) + "\n")
    grouped = {}
    for row in rows:
        grouped.setdefault(json.dumps(row["config"], sort_keys=True), []).append(row)
    summary = [summarize(r) for r in grouped.values()]
    with open(out / "summary.json", "w", encoding="utf8", newline="\n") as f:
        json.dump(summary, f, indent=1, sort_keys=True)
    fields = sorted({k for s in summary for k in s})
    with open(out / "summary.csv", "w", encoding="utf8", newline="") as f:
        writer = csv.DictWriter(f, fields)
        writer.writeheader()
        writer.writerows(summary)
    with open(out / "meta.json", "w", encoding="utf8", newline="\n") as f:
        json.dump(metadata(args.seeds), f, indent=1)


if __name__ == "__main__":
    main()
