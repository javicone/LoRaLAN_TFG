#!/usr/bin/env python3
"""Figuras de los experimentos, a partir de summary.json.

    python experiments/plot.py --in results/perf
"""
import argparse
import json
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

BLUE = "#2a78d6"
ORANGE = "#eb6834"
AQUA = "#1baf7a"
INK = "#0b0b0b"
MUTED = "#52514e"
GRID = "#e4e3df"


def style(ax, title, xlabel, ylabel):
    ax.set_title(title, loc="left", fontsize=10, color=INK)
    ax.set_xlabel(xlabel, fontsize=9, color=MUTED)
    ax.set_ylabel(ylabel, fontsize=9, color=MUTED)
    ax.grid(axis="y", color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    ax.tick_params(colors=MUTED, labelsize=8)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(GRID)


def line(ax, xs, ys, err, color, label, marker="o"):
    ax.errorbar(xs, ys, yerr=err, color=color, linewidth=2, marker=marker, markersize=6,
                markeredgecolor="white", markeredgewidth=1.5, capsize=3, label=label)


def rows(summary, exp, **filters):
    return [s for s in summary if s["exp"] == exp and all(s.get(k) == v for k, v in filters.items())]


def fig_exp1(summary, out):
    ev = sorted(rows(summary, "EXP1E"), key=lambda s: s["loss"])
    val = sorted(rows(summary, "VAL"), key=lambda s: s["loss"])
    poll = sorted(rows(summary, "EXP1P"), key=lambda s: s["loss"])
    fig, (a, b) = plt.subplots(1, 2, figsize=(9, 3.4))
    xs = [s["loss"] for s in ev]
    a.plot(xs, [100 * (1 - (x / 100) ** 3) for x in xs], color=MUTED, linewidth=1.5, linestyle="--", label="Teoría: 1 − p³ (solo ida)")
    line(a, [s["loss"] for s in val], [s["delivered_pct"] for s in val],
         [[s["delivered_pct"] - s["delivered_ic_lo"] for s in val], [s["delivered_ic_hi"] - s["delivered_pct"] for s in val]],
         AQUA, "Evento, pérdida solo en ida", "s")
    line(a, xs, [s["delivered_pct"] for s in ev],
         [[s["delivered_pct"] - s["delivered_ic_lo"] for s in ev], [s["delivered_ic_hi"] - s["delivered_pct"] for s in ev]],
         BLUE, "Evento, pérdida en ambos sentidos")
    a.plot([s["loss"] for s in poll], [100 * s["responses"] / s["polls"] for s in poll], color=ORANGE, linewidth=2,
           marker="^", markersize=6, markeredgecolor="white", markeredgewidth=1.5, label="Respuesta a un POLL")
    style(a, "Entrega", "Pérdida de trama (%)", "Entregados (%)")
    a.set_ylim(0, 105)
    a.legend(frameon=False, fontsize=7.5, loc="lower left")
    line(b, xs, [s["lat_p95"] for s in ev], [s["lat_p95_ic"] for s in ev], ORANGE, "p95")
    line(b, xs, [s["lat_p50"] for s in ev], [s["lat_p50_ic"] for s in ev], BLUE, "Mediana", "s")
    style(b, "Latencia de un evento entregado", "Pérdida de trama (%)", "Milisegundos")
    b.set_ylim(bottom=0)
    b.legend(frameon=False, fontsize=8, loc="upper left")
    fig.tight_layout()
    fig.savefig(out / "exp1.png", dpi=200)
    plt.close(fig)


def fig_exp45(summary, out):
    fig, axes = plt.subplots(1, 2, figsize=(9, 3.4), sharey=True)
    for ax, nodes in zip(axes, (8, 32)):
        for exp, color, label, mark in (("EXP4", BLUE, "Sin sondeo", "o"), ("EXP5", ORANGE, "Con sondeo cada 5 s", "s")):
            data = sorted((s for s in rows(summary, exp, nodes=nodes, rate=60, poll=0 if exp == "EXP4" else 5000)
                           if "loss" not in s), key=lambda s: s["blind"])
            ax.plot([s["blind"] for s in data], [100 * s["collided_push"] / s["tx_push"] for s in data], color=color,
                    linewidth=2, marker=mark, markersize=6, markeredgecolor="white", markeredgewidth=1.5, label=label)
        style(ax, f"{nodes} nodos, 60 fichajes por minuto", "Ventana ciega W (ms)", "Emisiones destruidas (%)" if nodes == 8 else "")
    axes[0].set_ylim(bottom=0)   # eje compartido: fijarlo dentro del bucle recortaba el 2.o panel
    axes[0].legend(frameon=False, fontsize=8)
    fig.tight_layout()
    fig.savefig(out / "exp4_exp5.png", dpi=200)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--in", dest="source", default=str(Path(__file__).resolve().parent.parent / "results" / "perf"))
    args = parser.parse_args()
    source = Path(args.source)
    summary = json.load(open(source / "summary.json", encoding="utf8"))
    out = source / "figuras"
    out.mkdir(exist_ok=True)
    fig_exp1(summary, out)
    fig_exp45(summary, out)


if __name__ == "__main__":
    main()
