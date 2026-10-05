#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
BASE_DIR=$(cd -- "${SCRIPT_DIR}/.." && pwd)

"${SCRIPT_DIR}/capture_environment.sh"
"${SCRIPT_DIR}/smoke_test.sh"
for case in case1 case2 case3 case4; do
    "${SCRIPT_DIR}/run_case.sh" "${case}"
done
python3 "${SCRIPT_DIR}/collect_results.py"
"${SCRIPT_DIR}/verify_all.sh"
"${SCRIPT_DIR}/test_failure_propagation.sh"
date --iso-8601=seconds > "${BASE_DIR}/results/last_successful_run.txt"
echo "official baseline run completed successfully"
