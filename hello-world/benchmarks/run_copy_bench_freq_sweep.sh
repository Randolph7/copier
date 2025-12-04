#!/bin/bash
set -euo pipefail
set -x

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
RESULTS_ROOT="${RESULTS_ROOT:-$SCRIPT_DIR/results_freq}"
RUN_ID="$(date +%Y%m%d_%H%M%S)"
RUN_DIR="$RESULTS_ROOT/$RUN_ID"
LOG_DIR="$RUN_DIR/logs"
PLOT_DIR="$RUN_DIR/plots"
SUMMARY_FILE="$RUN_DIR/summary.jsonl"

TARGET_NUMA_NODE="${TARGET_NUMA_NODE:-0}"
CPU_LIST="${CPU_LIST:-0 1 2 3 4 5 6 7 8 9}"
MAX_FREQ_GHZ="${MAX_FREQ_GHZ:-4}"
GOVERNOR="${GOVERNOR:-userspace}"
SIZE_ARG="${SIZE_ARG:-10G}"
ROUNDS="${ROUNDS:-5}"
GRANULARITY_ARG="${GRANULARITY_ARG:-4K}"
RAPL_PATH_DEFAULT="/sys/class/powercap/intel-rapl:0/energy_uj"

NUMACTL_BIN="$(command -v numactl || true)"
SUDO_BIN="$(command -v sudo || true)"

if ! command -v cpufreq-set >/dev/null 2>&1; then
	sudo apt install -y cpufrequtils
fi

if ! command -v cpufreq-set >/dev/null 2>&1; then
	echo "[ERROR] cpufreq-set unavailable after installation attempt." >&2
	exit 1
fi

if [[ -z "$NUMACTL_BIN" ]]; then
	echo "[ERROR] numactl is required to pin workload to NUMA node $TARGET_NUMA_NODE." >&2
	exit 1
fi

if [[ -z "$SUDO_BIN" ]]; then
	echo "[ERROR] sudo is required for cpufreq and energy access." >&2
	exit 1
fi

mkdir -p "$LOG_DIR" "$PLOT_DIR"

echo "[INFO] Results directory: $RUN_DIR"
echo "[INFO] Building copy_bench..."
(cd "$PROJECT_ROOT" && make benchmarks/copy_bench >/dev/null)

declare -a FREQ_VALUES=("$@")
if [[ ${#FREQ_VALUES[@]} -eq 0 ]]; then
	FREQ_VALUES=(1.0 1.2 1.4 1.6 1.8 2.0 2.2 2.4 2.6 2.8 3.0 3.2)
fi

read_energy() {
	local rapl_path="${RAPL_PATH:-$RAPL_PATH_DEFAULT}"
	if [[ -r "$rapl_path" ]]; then
		local reader=("$SUDO_BIN" cat "$rapl_path")
		if "${reader[@]}" 2>/dev/null; then
			return 0
		fi
	fi
	echo "NA"
}

apply_frequency() {
	local freq="$1"
	local freq_str="${freq}GHz"
	for cpu in $CPU_LIST; do
		sudo cpufreq-set -c "$cpu" -g "$GOVERNOR"
		sudo cpufreq-set -c "$cpu" -d "$freq_str" -u "${MAX_FREQ_GHZ}GHz"
		sudo cpufreq-set -c "$cpu" -f "$freq_str"
	done
}

append_summary() {
	local json_line="$1"
	local mode="$2"
	local freq="$3"
	local energy="$4"
	JSON_LINE="$json_line" python3 - "$SUMMARY_FILE" "$RUN_ID" "$mode" "$freq" "$energy" <<'PY'
import json
import os
import sys
from pathlib import Path

json_line = os.environ["JSON_LINE"]
summary_path = Path(sys.argv[1])
run_id = sys.argv[2]
mode = sys.argv[3]
freq = float(sys.argv[4])
energy_arg = sys.argv[5]
energy = None if energy_arg == "null" else float(energy_arg)

data = json.loads(json_line)
record = {
    "run_id": run_id,
    "mode": mode,
    "frequency_ghz": freq,
    "size_bytes": data["size_bytes"],
    "rounds": data["rounds"],
    "granularity_bytes": data.get("granularity_bytes"),
    "avg_ns": data["avg_ns"],
    "min_ns": data["min_ns"],
    "max_ns": data["max_ns"],
    "throughput_gib_s": data["throughput_gib_s"],
    "pkg_energy_j": energy,
}

summary_path.parent.mkdir(parents=True, exist_ok=True)
with summary_path.open("a", encoding="utf-8") as fh:
    json.dump(record, fh)
    fh.write("\n")
PY
}

run_mode() {
	local mode="$1"
	local freq="$2"
	local cmd=("$PROJECT_ROOT/benchmarks/copy_bench" "--mode" "$mode" "--size" "$SIZE_ARG" "--rounds" "$ROUNDS" "--granularity" "$GRANULARITY_ARG")
	local pinned_cmd=("$NUMACTL_BIN" "--cpunodebind=$TARGET_NUMA_NODE" "--membind=$TARGET_NUMA_NODE" "${cmd[@]}")

	local log_file="$LOG_DIR/freq_${freq}GHz_${mode}.log"
	local tmp_output
	tmp_output="$(mktemp)"

	local start_energy end_energy energy_json
	start_energy="$(read_energy)"
	set +e
	"${pinned_cmd[@]}" >"$tmp_output" 2>&1
	local rc=$?
	set -e
	end_energy="$(read_energy)"

	cat "$tmp_output" | tee "$log_file"

	if [[ $rc -ne 0 ]]; then
		rm -f "$tmp_output"
		echo "[ERROR] Benchmark failed for mode ${mode} at ${freq}GHz" >&2
		exit $rc
	fi

	local json_line
	json_line="$(grep -E '^\{' "$tmp_output" | tail -n1 || true)"
	rm -f "$tmp_output"

	if [[ -z "$json_line" ]]; then
		echo "[ERROR] JSON summary missing for mode ${mode} at ${freq}GHz" >&2
		exit 1
	fi

	energy_json="null"
	if [[ "$start_energy" != "NA" && "$end_energy" != "NA" ]]; then
		energy_json="$(python3 - "$start_energy" "$end_energy" <<'PY'
import sys
start = int(sys.argv[1])
end = int(sys.argv[2])
diff = end - start
if diff < 0:
    diff += 1 << 32
print(diff / 1_000_000.0)
PY
)"
	fi

	append_summary "$json_line" "$mode" "$freq" "$energy_json"
}

for freq in "${FREQ_VALUES[@]}"; do
	echo "[INFO] === Setting CPUs ($CPU_LIST) to ${freq}GHz (NUMA node ${TARGET_NUMA_NODE}) ==="
	apply_frequency "$freq"
	run_mode "memcpy" "$freq"
	run_mode "async" "$freq"
done

PLOT_FILE="$PLOT_DIR/freq_sweep_${RUN_ID}.png"

python3 - "$SUMMARY_FILE" "$PLOT_FILE" <<'PY'
import json
import sys
from pathlib import Path

import matplotlib.pyplot as plt

summary_path = Path(sys.argv[1])
output_path = Path(sys.argv[2])

records = []
with summary_path.open("r", encoding="utf-8") as fh:
    for line in fh:
        line = line.strip()
        if not line:
            continue
        records.append(json.loads(line))

if not records:
    raise SystemExit("No records found for plotting.")

freqs = sorted({rec["frequency_ghz"] for rec in records})
modes = sorted({rec["mode"] for rec in records})

def get_series(key):
    series = {mode: [] for mode in modes}
    for freq in freqs:
        for mode in modes:
            match = next((rec for rec in records if rec["mode"] == mode and rec["frequency_ghz"] == freq), None)
            series[mode].append(match[key] if match else None)
    return series

throughput = get_series("throughput_gib_s")
energy = get_series("pkg_energy_j")
energy_available = any(val is not None for vals in energy.values() for val in vals if val is not None)

ratio = {mode: [] for mode in modes}
if energy_available:
    for freq in freqs:
        for mode in modes:
            match = next((rec for rec in records if rec["mode"] == mode and rec["frequency_ghz"] == freq), None)
            if match and match.get("pkg_energy_j"):
                energy_val = match["pkg_energy_j"]
                ratio[mode].append(match["throughput_gib_s"] / energy_val if energy_val > 0 else None)
            else:
                ratio[mode].append(None)

if energy_available:
    fig, axes = plt.subplots(1, 3, figsize=(13, 4))
    tp_ax, energy_ax, ratio_ax = axes
else:
    fig, tp_ax = plt.subplots(1, 1, figsize=(5, 4))
    energy_ax = None
    ratio_ax = None

for mode in modes:
    tp_ax.plot(freqs, throughput[mode], marker="o", label=mode)
tp_ax.set_xlabel("Frequency (GHz)")
tp_ax.set_ylabel("Throughput (GiB/s)")
tp_ax.set_title("Throughput vs Frequency")
tp_ax.grid(True, linestyle="--", alpha=0.3)
tp_ax.legend()

if energy_available and energy_ax:
    for mode in modes:
        energy_ax.plot(freqs, energy[mode], marker="o", label=mode)
    energy_ax.set_xlabel("Frequency (GHz)")
    energy_ax.set_ylabel("Package Energy (J)")
    energy_ax.set_title("Energy vs Frequency")
    energy_ax.grid(True, linestyle="--", alpha=0.3)
    energy_ax.legend()

    for mode in modes:
        ratio_data = ratio[mode]
        ratio_ax.plot(freqs, ratio_data, marker="o", label=mode)
    ratio_ax.set_xlabel("Frequency (GHz)")
    ratio_ax.set_ylabel("Throughput/Energy (GiB/J)")
    ratio_ax.set_title("Efficiency vs Frequency")
    ratio_ax.grid(True, linestyle="--", alpha=0.3)
    ratio_ax.legend()
else:
    tp_ax.text(0.5, -0.2, "Energy data unavailable", transform=tp_ax.transAxes, ha="center")

fig.suptitle("Copy Benchmark Frequency Sweep")
fig.tight_layout()
output_path.parent.mkdir(parents=True, exist_ok=True)
fig.savefig(output_path, dpi=200)
PY

echo "[INFO] Summary JSONL : $SUMMARY_FILE"
echo "[INFO] Plot saved to  : $PLOT_FILE"
echo "[INFO] Logs directory : $LOG_DIR"

set +x
