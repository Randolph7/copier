#!/usr/bin/env python3
import argparse
import json
from pathlib import Path
from typing import Dict, List

import matplotlib.pyplot as plt


def load_records(path: Path) -> List[Dict]:
    records = []
    if not path.exists():
        raise FileNotFoundError(f"{path} not found")
    with path.open("r", encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError:
                continue
    return records


def filter_by_run_id(records: List[Dict], run_id: str) -> List[Dict]:
    return [rec for rec in records if rec.get("run_id") == run_id]


def ensure_modes(records: List[Dict]) -> List[str]:
    modes = []
    for preferred in ("memcpy", "async"):
        if any(rec.get("mode") == preferred for rec in records):
            modes.append(preferred)
    for rec in records:
        mode = rec.get("mode")
        if mode not in modes:
            modes.append(mode)
    return modes


def plot(records: List[Dict], output: Path, title: str) -> None:
    modes = ensure_modes(records)
    throughput = [next(rec for rec in records if rec["mode"] == mode)["throughput_gib_s"] for mode in modes]
    energy_values = []
    energy_available = any(rec.get("pkg_energy_j") is not None for rec in records)
    if energy_available:
        for mode in modes:
            rec = next(rec for rec in records if rec["mode"] == mode)
            energy_values.append(rec.get("pkg_energy_j"))

    ratio_values = []
    if energy_available:
        for tp, energy in zip(throughput, energy_values):
            if energy is None or energy <= 0:
                ratio_values.append(None)
            else:
                ratio_values.append(tp / energy)

    if energy_available:
        fig, axes = plt.subplots(1, 3, figsize=(13, 4))
        tp_ax, energy_ax, ratio_ax = axes
    else:
        fig, tp_ax = plt.subplots(1, 1, figsize=(5, 4))
        energy_ax = None
        ratio_ax = None

    fig.suptitle(title)

    tp_ax.bar(modes, throughput, color=["#4f6bed", "#f28e2b"][: len(modes)])
    tp_ax.set_ylabel("Throughput (GiB/s)")
    tp_ax.set_ylim(0, max(throughput) * 1.2 if throughput else 1)
    for idx, val in enumerate(throughput):
        tp_ax.text(idx, val, f"{val:.2f}", ha="center", va="bottom")

    if energy_available and energy_ax and ratio_ax:
        energy_clean = [val if val is not None else 0 for val in energy_values]
        energy_ax.bar(modes, energy_clean, color=["#59a14f", "#e15759"][: len(modes)])
        energy_ax.set_ylabel("Package Energy (J)")
        ymax = max(ev for ev in energy_clean if ev is not None) if any(energy_clean) else 1
        energy_ax.set_ylim(0, ymax * 1.2 if ymax else 1)
        for idx, val in enumerate(energy_values):
            label = "N/A" if val is None else f"{val:.2f}"
            energy_ax.text(idx, energy_clean[idx], label, ha="center", va="bottom")

        ratio_clean = [val if val is not None else 0 for val in ratio_values]
        ratio_ax.bar(modes, ratio_clean, color=["#af7aa1", "#ff9da7"][: len(modes)])
        ratio_ax.set_ylabel("Throughput/Energy (GiB/J)")
        ymax_ratio = max((val for val in ratio_clean if val is not None), default=1)
        ratio_ax.set_ylim(0, ymax_ratio * 1.2 if ymax_ratio else 1)
        for idx, val in enumerate(ratio_values):
            label = "N/A" if val is None else f"{val:.3f}"
            ratio_ax.text(idx, ratio_clean[idx], label, ha="center", va="bottom")
    elif not energy_available:
        tp_ax.text(0.5, -0.2, "Energy data unavailable (RAPL not readable)", ha="center", transform=tp_ax.transAxes)

    fig.tight_layout()
    output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output, dpi=200)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description="Plot copy benchmark comparison.")
    parser.add_argument("--input", required=True, help="Path to JSONL results file")
    parser.add_argument("--run-id", required=True, help="Run identifier to plot")
    parser.add_argument("--output", required=True, help="Destination PNG path")
    parser.add_argument("--title", default="Copy Benchmark", help="Plot title")
    args = parser.parse_args()

    records = load_records(Path(args.input))
    run_records = filter_by_run_id(records, args.run_id)
    if not run_records:
        raise SystemExit(f"No records found for run-id {args.run_id}")

    plot(run_records, Path(args.output), args.title)


if __name__ == "__main__":
    main()

