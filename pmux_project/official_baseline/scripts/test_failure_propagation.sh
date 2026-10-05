#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
if "${SCRIPT_DIR}/run_case.sh" deliberately_invalid_case >/dev/null 2>&1; then
    echo "run_case.sh incorrectly reported success for an invalid case" >&2
    exit 1
fi
echo "failure-propagation negative control: PASS"
