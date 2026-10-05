#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
BASE_DIR=$(cd -- "${SCRIPT_DIR}/.." && pwd)
YOSYS="${BASE_DIR}/toolchain/install/bin/yosys"
ABC="${BASE_DIR}/toolchain/install/bin/yosys-abc"
EQY="${BASE_DIR}/toolchain/install/bin/eqy"
SBY="${BASE_DIR}/toolchain/install/bin/sby"
OUT="${BASE_DIR}/environment/toolchain.txt"

for tool in "${YOSYS}" "${ABC}" "${EQY}" "${SBY}"; do
    [[ -x "${tool}" ]] || { echo "missing executable: ${tool}" >&2; exit 1; }
done

{
    echo "Captured (Asia/Shanghai): $(TZ=Asia/Shanghai date --iso-8601=seconds)"
    echo
    echo "[Operating system / WSL]"
    grep -E '^(PRETTY_NAME|VERSION_ID)=' /etc/os-release
    uname -a
    cat /proc/version
    echo
    echo "[Actual tool paths]"
    realpath "${YOSYS}"
    realpath "${ABC}"
    realpath "${EQY}"
    realpath "${SBY}"
    command -v gcc
    command -v g++
    command -v cmake
    command -v ninja
    command -v python3
    echo
    echo "[Versions]"
    "${YOSYS}" -V
    "${EQY}" --version
    "${SBY}" --version
    "${ABC}" -h 2>&1 | tail -n 3 || true
    gcc --version | head -n 1
    g++ --version | head -n 1
    cmake --version | head -n 1
    ninja --version
    python3 --version
    echo
    echo "[Pinned commits]"
    echo "Yosys: $(git -C "${BASE_DIR}/toolchain/yosys-v0.69-src" rev-parse HEAD)"
    echo "ABC: $(git -C "${BASE_DIR}/toolchain/yosys-v0.69-src" ls-tree HEAD abc | awk '{print $3}')"
    echo "EQY: $(git -C "${BASE_DIR}/toolchain/eqy-v0.69-src" rev-parse HEAD)"
    echo "SBY: $(git -C "${BASE_DIR}/toolchain/sby-v0.69-src" rev-parse HEAD)"
    echo
    echo "[Solver]"
    echo "EQY strategies use Yosys 0.69 built-in SAT solver (sat pass)."
    found=0
    for solver in z3 yices-smt2 bitwuzla boolector cvc5; do
        if command -v "${solver}" >/dev/null 2>&1; then
            found=1
            echo "${solver}: $(command -v "${solver}")"
            "${solver}" --version 2>&1 | head -n 1 || true
        fi
    done
    if [[ "${found}" -eq 0 ]]; then
        echo "No external SMT solver found; none is required by the selected EQY sat strategy."
    fi
    echo
    echo "[Integrity constraints]"
    echo "Required Yosys commit: 9f75ca1f9834a39a863915b5dae0c7b1e33533bc"
    echo "No custom plugin is loaded by any baseline script."
    echo "Synthesis command: synth_intel -family cycloneiv"
} > "${OUT}"

"${YOSYS}" -p "help synth_intel" > "${BASE_DIR}/environment/synth_intel_help.log" 2>&1
sed -n '120,360p' "${BASE_DIR}/toolchain/yosys-v0.69-src/techlibs/intel/synth_intel.cc" \
    > "${BASE_DIR}/environment/synth_intel_source_excerpt.txt"

echo "wrote ${OUT}"
