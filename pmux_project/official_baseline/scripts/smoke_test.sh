#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
BASE_DIR=$(cd -- "${SCRIPT_DIR}/.." && pwd)
YOSYS="${BASE_DIR}/toolchain/install/bin/yosys"
SOURCE="${BASE_DIR}/environment/minimal_abc9_smoke.v"
SCRIPT="${BASE_DIR}/environment/minimal_abc9_smoke.ys"
LOG="${BASE_DIR}/environment/minimal_abc9_smoke.log"
CONSOLE="${BASE_DIR}/environment/minimal_abc9_smoke.console.log"
COMMAND="${BASE_DIR}/environment/minimal_abc9_smoke.command.txt"

printf '%s\n' \
    "read_verilog \"${SOURCE}\";" \
    "synth_intel -family cycloneiv -top minimal_abc9_smoke;" \
    "stat;" \
    "write_rtlil \"${BASE_DIR}/environment/minimal_abc9_smoke_full.il\";" \
    > "${SCRIPT}"

printf '%q ' "${YOSYS}" -l "${LOG}" -s "${SCRIPT}" > "${COMMAND}"
printf '\n' >> "${COMMAND}"
set +e
"${YOSYS}" -l "${LOG}" -s "${SCRIPT}" > "${CONSOLE}" 2>&1
rc=$?
set -e
printf '%s\n' "${rc}" > "${BASE_DIR}/environment/minimal_abc9_smoke.returncode"
[[ "${rc}" -eq 0 ]] || exit "${rc}"
grep -q 'Executing ABC9' "${LOG}" || {
    echo "minimal smoke test did not execute ABC9" >&2
    exit 1
}
echo "minimal synth_intel/ABC9 smoke test: PASS"
