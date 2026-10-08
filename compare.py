#!/usr/bin/env python3
"""Compara los micro.csv de varias corridas: ops/s por hilos y cómo escala cada fase.

uso: ./compare.py results/<label-a> results/<label-b> [...]

"escala" = ops/s con el máximo de hilos / ops/s con 1 hilo. En un kernel que no
se traba, una fase de 16 hilos debería acercarse a 10x o más; si da ~1x o menos,
los hilos se están peleando por un lock.
"""
import csv
import sys
from collections import defaultdict
from pathlib import Path


def load(run_dir):
    data = defaultdict(dict)  # (grupo, fase) -> hilos -> fila
    with open(Path(run_dir) / "micro.csv") as f:
        for row in csv.DictReader(f):
            data[(row["grupo"], row["fase"])][int(row["hilos"])] = row
    return data


def fmt(ops):
    ops = float(ops)
    if ops >= 1e6:
        return f"{ops / 1e6:.1f}M"
    if ops >= 1e3:
        return f"{ops / 1e3:.0f}k"
    return f"{ops:.0f}"


def main(dirs):
    runs = {Path(d).name: load(d) for d in dirs}
    keys = list(dict.fromkeys(k for r in runs.values() for k in r))
    print("| fase | " + " | ".join(f"{name} ops/s (1→max hilos) | escala | sys% max" for name in runs) + " |")
    print("|---|" + "---|---|---|" * len(runs))
    for key in keys:
        cells = []
        for run in runs.values():
            by_threads = run.get(key)
            if not by_threads:
                cells += ["—", "—", "—"]
                continue
            lo, hi = by_threads[min(by_threads)], by_threads[max(by_threads)]
            cpu = float(hi["user_s"]) + float(hi["sys_s"])
            sys_pct = f"{100 * float(hi['sys_s']) / cpu:.0f}%" if cpu else "—"
            scale = float(hi["ops_s"]) / float(lo["ops_s"])
            cells += [f"{fmt(lo['ops_s'])} → {fmt(hi['ops_s'])}", f"{scale:.1f}x", sys_pct]
        print(f"| {key[0]}/{key[1]} | " + " | ".join(cells) + " |")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main(sys.argv[1:])
