#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 case1|case2|case3|case4" >&2
    exit 64
fi

CASE=$1
case "${CASE}" in
    case1) TOP=test1; SOURCE_NAME=test1.v ;;
    case2) TOP=test2; SOURCE_NAME=test2.v ;;
    case3) TOP=test3; SOURCE_NAME=test3.v ;;
    case4) TOP=test4; SOURCE_NAME=test4.v ;;
    *) echo "unknown official case: ${CASE}" >&2; exit 64 ;;
esac

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
BASE_DIR=$(cd -- "${SCRIPT_DIR}/.." && pwd)
WORKSPACE=$(cd -- "${BASE_DIR}/.." && pwd)
SOURCE="${WORKSPACE}/pmux_case/competition_case/${SOURCE_NAME}"
CASE_DIR="${BASE_DIR}/${CASE}"
LOG_DIR="${CASE_DIR}/logs"
NETLIST_DIR="${CASE_DIR}/netlists"
EQY_DIR="${CASE_DIR}/eqy"
YOSYS="${BASE_DIR}/toolchain/install/bin/yosys"
EQY="${BASE_DIR}/toolchain/install/bin/eqy"
PYTHON=python3
SAFE_PATH="${BASE_DIR}/toolchain/install/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"

mkdir -p "${LOG_DIR}" "${NETLIST_DIR}" "${EQY_DIR}"
: > "${LOG_DIR}/commands.log"

[[ -f "${SOURCE}" ]] || { echo "missing source: ${SOURCE}" >&2; exit 2; }
version=$("${YOSYS}" -V)
if [[ "${version}" != *'Yosys 0.69 '* || "${version}" == *dirty* || "${version}" == *post* ]]; then
    echo "refusing non-pristine/non-0.69 Yosys: ${version}" >&2
    exit 2
fi

record_command() {
    printf '%q ' "$@" >> "${LOG_DIR}/commands.log"
    printf '\n' >> "${LOG_DIR}/commands.log"
}

run_yosys() {
    local label=$1
    local script_text=$2
    local ys_file="${LOG_DIR}/${label}.ys"
    local log_file="${LOG_DIR}/${label}.log"
    local console_file="${LOG_DIR}/${label}.console.log"
    local time_file="${LOG_DIR}/${label}.time.txt"
    local rc_file="${LOG_DIR}/${label}.returncode"
    printf '%s\n' "${script_text}" > "${ys_file}"
    local cmd=(/usr/bin/time -v -o "${time_file}" "${YOSYS}" -l "${log_file}" -s "${ys_file}")
    record_command "${cmd[@]}"
    set +e
    "${cmd[@]}" > "${console_file}" 2>&1
    local rc=$?
    set -e
    printf '%s\n' "${rc}" > "${rc_file}"
    if [[ "${rc}" -ne 0 ]]; then
        echo "${CASE}: Yosys step ${label} failed (rc=${rc})" >&2
        return "${rc}"
    fi
}

run_eqy() {
    local label=$1
    local config="${EQY_DIR}/${label}.eqy"
    local work="${EQY_DIR}/${label}_work"
    local log="${EQY_DIR}/${label}.log"
    local rc_file="${EQY_DIR}/${label}.returncode"
    local cmd=(env "PATH=${SAFE_PATH}" "${EQY}" -f -d "${work}" "${config}")
    record_command "${cmd[@]}"
    set +e
    "${cmd[@]}" > "${log}" 2>&1
    local rc=$?
    set -e
    printf '%s\n' "${rc}" > "${rc_file}"
    return "${rc}"
}

write_eqy_configs() {
    cat > "${EQY_DIR}/default.eqy" <<EOF
[gold]
read_verilog -formal ${SOURCE}

[gate]
read_rtlil ${NETLIST_DIR}/post_abc9.il
read_verilog -formal -overwrite ${BASE_DIR}/environment/dffeas_formal.v

[script]
hierarchy -check -top ${TOP}
prep
memory_map
async2sync

[strategy sat]
use sat
depth 20
EOF

    cat > "${EQY_DIR}/whole.eqy" <<EOF
[gold]
read_verilog -formal ${SOURCE}

[gate]
read_rtlil ${NETLIST_DIR}/post_abc9.il
read_verilog -formal -overwrite ${BASE_DIR}/environment/dffeas_formal.v

[script]
hierarchy -check -top ${TOP}
prep
memory_map
async2sync

[partition ${TOP}]
merge *

[strategy sat]
use sat
depth 20

[strategy pdr]
use sby
engine abc pdr
timeout 300
EOF
}

FULL_SCRIPT="read_verilog \"${SOURCE}\";
synth_intel -family cycloneiv -top ${TOP};
stat;
write_rtlil \"${NETLIST_DIR}/full.il\";"

POST_SCRIPT="read_verilog \"${SOURCE}\";
synth_intel -family cycloneiv -top ${TOP} -run begin:map_cells;
stat;
write_json \"${NETLIST_DIR}/post_abc9.json\";
select ${TOP};
write_rtlil -selected \"${NETLIST_DIR}/post_abc9.il\";"

PROC_SCRIPT="read_verilog \"${SOURCE}\";
hierarchy -check -top ${TOP};
proc;
stat;
write_json \"${NETLIST_DIR}/stage_proc.json\";
select ${TOP};
write_rtlil -selected \"${NETLIST_DIR}/stage_proc.il\";"

OPT_SCRIPT="read_verilog \"${SOURCE}\";
hierarchy -check -top ${TOP};
proc;
opt;
stat;
write_json \"${NETLIST_DIR}/stage_opt.json\";
select ${TOP};
write_rtlil -selected \"${NETLIST_DIR}/stage_opt.il\";"

PRE_SCRIPT="read_verilog \"${SOURCE}\";
synth_intel -family cycloneiv -top ${TOP} -run begin:map_luts;
stat;
write_json \"${NETLIST_DIR}/stage_pre_abc9.json\";
select ${TOP};
write_rtlil -selected \"${NETLIST_DIR}/stage_pre_abc9.il\";"

BEFORE_TECHMAP_SCRIPT="read_verilog \"${SOURCE}\";
synth_intel -family cycloneiv -top ${TOP} -run begin:map_ffram;
opt -fast -mux_undef -undriven -fine -full;
memory_map;
opt -undriven -fine;
stat;
write_json \"${NETLIST_DIR}/stage_before_common_techmap.json\";
select ${TOP};
write_rtlil -selected \"${NETLIST_DIR}/stage_before_common_techmap.il\";"

AFTER_TECHMAP_SCRIPT="read_verilog \"${SOURCE}\";
synth_intel -family cycloneiv -top ${TOP} -run begin:map_ffram;
opt -fast -mux_undef -undriven -fine -full;
memory_map;
opt -undriven -fine;
techmap -map +/techmap.v;
stat;
write_json \"${NETLIST_DIR}/stage_after_common_techmap.json\";
select ${TOP};
write_rtlil -selected \"${NETLIST_DIR}/stage_after_common_techmap.il\";"

run_yosys full "${FULL_SCRIPT}"
run_yosys post_abc9 "${POST_SCRIPT}"

# Keep each observation in an independently reset design, but run the group in
# one Yosys process.  This removes four WSL/process startups without changing
# any pass sequence or netlist content.
OBSERVATION_SCRIPT="${PROC_SCRIPT}
design -reset;
${OPT_SCRIPT}
design -reset;
${BEFORE_TECHMAP_SCRIPT}
design -reset;
${AFTER_TECHMAP_SCRIPT}
design -reset;
${PRE_SCRIPT}"
run_yosys stage_observations "${OBSERVATION_SCRIPT}"

for stage in proc opt pre_abc9 before_common_techmap after_common_techmap; do
    "${PYTHON}" "${SCRIPT_DIR}/analyze_netlist.py" \
        --json "${NETLIST_DIR}/stage_${stage}.json" --top "${TOP}" \
        --output "${NETLIST_DIR}/stage_${stage}_metrics.json"
done
"${PYTHON}" "${SCRIPT_DIR}/analyze_netlist.py" \
    --json "${NETLIST_DIR}/post_abc9.json" --top "${TOP}" \
    --output "${NETLIST_DIR}/post_abc9_metrics.json" --validate-dffeas

if ! grep -Fq 'cell $lut' "${NETLIST_DIR}/post_abc9.il"; then
    echo "${CASE}: post-ABC9 RTLIL does not contain a \$lut cell" >&2
    exit 3
fi
lut_count=$("${PYTHON}" -c 'import json,sys; print(json.load(open(sys.argv[1]))["lut_count"])' "${NETLIST_DIR}/post_abc9_metrics.json")
if [[ "${lut_count}" -le 0 ]]; then
    echo "${CASE}: post-ABC9 LUT count is not positive" >&2
    exit 3
fi

write_eqy_configs

if run_eqy default; then
    printf 'PASS (default partition)\n' > "${EQY_DIR}/status.txt"
    cp "${EQY_DIR}/default.log" "${EQY_DIR}/final.log"
else
    default_rc=$(<"${EQY_DIR}/default.returncode")
    echo "${CASE}: default partition did not prove (rc=${default_rc}); trying whole-design partition" >&2
    if run_eqy whole; then
        printf 'PASS (whole-design fallback; default rc=%s)\n' "${default_rc}" > "${EQY_DIR}/status.txt"
        cp "${EQY_DIR}/whole.log" "${EQY_DIR}/final.log"
    else
        whole_rc=$(<"${EQY_DIR}/whole.returncode")
        printf 'FAIL (default rc=%s; whole rc=%s)\n' "${default_rc}" "${whole_rc}" > "${EQY_DIR}/status.txt"
        echo "${CASE}: EQY failed in default and whole-design modes" >&2
        exit 4
    fi
fi

grep -q 'DONE (PASS' "${EQY_DIR}/final.log" || {
    echo "${CASE}: EQY returned success without an explicit PASS marker" >&2
    exit 4
}

echo "${CASE}: PASS (${lut_count} post-ABC9 LUTs)"
