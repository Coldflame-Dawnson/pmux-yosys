#!/usr/bin/env bash
set -u

root=/home/dawnson/work/pmux-yosys-a-case12-audit
cd "$root"

report=pmux_project/reports/a-case12-width-reaudit-4c43ab9
tool_bin="$root/pmux_project/official_baseline/toolchain/install/bin"
mkdir -p "$report/random-default-1000-retry" "$report/random-explicit-case12-1000-retry"

python3 pmux_project/scripts/run_random.py \
  --count 1000 \
  --mode external \
  --external-kind real \
  --bin "$tool_bin" \
  --out "$report/random-default-1000-retry" \
  --dut-template 'python3 {project}/scripts/real_pass_adapter.py --before {before} --after {after} --plan {plan} --stats {stats} --plugin {project}/pass/pmux_opt.so --yosys {project}/official_baseline/toolchain/install/bin/yosys' \
  > "$report/random-default-1000-retry.console.log" 2>&1 &
default_pid=$!

python3 pmux_project/scripts/run_random.py \
  --count 1000 \
  --mode external \
  --external-kind real \
  --bin "$tool_bin" \
  --out "$report/random-explicit-case12-1000-retry" \
  --dut-template 'python3 {project}/experiments/case12_v02/a_width_reaudit/real_case12_adapter.py --before {before} --after {after} --plan {plan} --stats {stats} --plugin {project}/pass/pmux_opt.so --yosys {project}/official_baseline/toolchain/install/bin/yosys' \
  > "$report/random-explicit-case12-1000-retry.console.log" 2>&1 &
explicit_pid=$!

wait "$default_pid"
default_rc=$?
wait "$explicit_pid"
explicit_rc=$?

printf 'DEFAULT_RC=%s EXPLICIT_RC=%s\n' "$default_rc" "$explicit_rc"
if [[ "$default_rc" -ne 0 || "$explicit_rc" -ne 0 ]]; then
  exit 1
fi
