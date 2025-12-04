#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
RESULTS_DIR="$SCRIPT_DIR/results"
JSONL_FILE="$RESULTS_DIR/copy_bench_results.jsonl"
RUN_ID="$(date +%Y%m%d_%H%M%S)"
LOG_FILE="$RESULTS_DIR/copy_bench_${RUN_ID}.log"
PLOT_FILE="$RESULTS_DIR/copy_bench_${RUN_ID}.png"
PLOT_SCRIPT="$SCRIPT_DIR/plot_copy_bench.py"

SIZE_ARG="10G"
ROUNDS=5
GRANULARITY_ARG="4K"
RAPL_PATH_DEFAULT="/sys/class/powercap/intel-rapl:0/energy_uj"
NUMA_NODE="${NUMA_NODE:-0}"
NUMACTL_BIN="$(command -v numactl || true)"
SUDO_BIN="$(command -v sudo || true)"

usage() {
	cat <<EOF
Usage: $(basename "$0") [options]

Options:
  -s SIZE         Total bytes to copy (supports suffix K/M/G). Default: 1G
  -r ROUNDS       Number of iterations per mode. Default: 5
  -g GRANULARITY  Copy granularity (bytes, supports suffix). Default: 4K
  -o DIR          Directory to store logs/results/plots. Default: benchmarks/results
  -h              Show this help message

Environment:
  RAPL_PATH   Override energy counter path (default: ${RAPL_PATH_DEFAULT})
  NUMA_NODE   NUMA node id to pin benchmark to (default: ${NUMA_NODE})
EOF
}

while getopts ":s:r:g:o:h" opt; do
	case "$opt" in
	s)
		SIZE_ARG="$OPTARG"
		;;
	r)
		ROUNDS="$OPTARG"
		;;
	g)
		GRANULARITY_ARG="$OPTARG"
		;;
	o)
		RESULTS_DIR="$OPTARG"
		JSONL_FILE="$RESULTS_DIR/copy_bench_results.jsonl"
		LOG_FILE="$RESULTS_DIR/copy_bench_${RUN_ID}.log"
		PLOT_FILE="$RESULTS_DIR/copy_bench_${RUN_ID}.png"
		;;
	h)
		usage
		exit 0
		;;
	\?)
		echo "Unknown option: -$OPTARG" >&2
		usage
		exit 1
		;;
	:)
		echo "Option -$OPTARG requires an argument." >&2
		exit 1
		;;
	esac
done

mkdir -p "$RESULTS_DIR"

if [[ -z "$NUMACTL_BIN" ]]; then
	echo "[ERROR] numactl is required to pin execution to NUMA node 0." >&2
	exit 1
fi

echo "[INFO] Building copy_bench binary..."
(cd "$PROJECT_ROOT" && make benchmarks/copy_bench >/dev/null)

read_energy() {
	local rapl_path="${RAPL_PATH:-$RAPL_PATH_DEFAULT}"
	if [[ -r "$rapl_path" ]]; then
		local reader_cmd=("cat" "$rapl_path")
		if [[ -n "$SUDO_BIN" ]]; then
			reader_cmd=("$SUDO_BIN" "${reader_cmd[@]}")
		fi
		if "${reader_cmd[@]}" 2>/dev/null; then
			return 0
		fi
	fi
	echo "NA"
}

record_result() {
	local json_line="$1"
	local mode="$2"
	local energy_value="$3"
	JSON_LINE="$json_line" python3 - "$JSONL_FILE" "$RUN_ID" "$mode" "$energy_value" <<'PY'
import json
import os
import sys
import time
from pathlib import Path

json_line = os.environ["JSON_LINE"]
data = json.loads(json_line)
jsonl_path = Path(sys.argv[1])
run_id = sys.argv[2]
mode = sys.argv[3]
energy_arg = sys.argv[4]
energy = None if energy_arg == "null" else float(energy_arg)

record = {
    "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    "run_id": run_id,
    "mode": mode,
    "size_bytes": data["size_bytes"],
    "rounds": data["rounds"],
    "granularity_bytes": data.get("granularity_bytes"),
    "avg_ns": data["avg_ns"],
    "min_ns": data["min_ns"],
    "max_ns": data["max_ns"],
    "throughput_gib_s": data["throughput_gib_s"],
    "pkg_energy_j": energy,
}

jsonl_path.parent.mkdir(parents=True, exist_ok=True)
with jsonl_path.open("a", encoding="utf-8") as fh:
    json.dump(record, fh)
    fh.write("\n")
print(f"[INFO] Result appended to {jsonl_path}")
PY
}

run_mode() {
	local mode="$1"
	local cmd=("$PROJECT_ROOT/benchmarks/copy_bench" "--mode" "$mode" "--size" "$SIZE_ARG" "--rounds" "$ROUNDS" "--granularity" "$GRANULARITY_ARG")
	local pinned_cmd=("$NUMACTL_BIN" "--cpunodebind=$NUMA_NODE" "--membind=$NUMA_NODE" "${cmd[@]}")
	local tmp_output
	tmp_output="$(mktemp)"
	local start_energy end_energy energy_json

	start_energy="$(read_energy)"
	set +e
	"${pinned_cmd[@]}" >"$tmp_output" 2>&1
	local rc=$?
	set -e
	end_energy="$(read_energy)"

	cat "$tmp_output" | tee -a "$LOG_FILE"
	if [[ $rc -ne 0 ]]; then
		rm -f "$tmp_output"
		echo "[ERROR] Benchmark failed for mode ${mode}" >&2
		exit $rc
	fi

	local json_line
	json_line="$(grep -E '^\{' "$tmp_output" | tail -n1 || true)"
	rm -f "$tmp_output"
	if [[ -z "$json_line" ]]; then
		echo "[ERROR] JSON summary missing in benchmark output for mode ${mode}" >&2
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

	record_result "$json_line" "$mode" "$energy_json"
}

echo "[INFO] Logging to $LOG_FILE"
run_mode "memcpy"
run_mode "async"

echo "[INFO] Generating comparison plot..."
python3 "$PLOT_SCRIPT" --input "$JSONL_FILE" --run-id "$RUN_ID" --output "$PLOT_FILE" --title "Copy Benchmark ($SIZE_ARG, ${ROUNDS}x)"
echo "[INFO] Plot written to $PLOT_FILE"
echo "[INFO] Done. Logs in $LOG_FILE"

