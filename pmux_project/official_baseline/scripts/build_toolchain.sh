#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
BASE_DIR=$(cd -- "${SCRIPT_DIR}/.." && pwd)
SRC_DIR="${BASE_DIR}/toolchain/yosys-v0.69-src"
BUILD_DIR="${BASE_DIR}/toolchain/yosys-v0.69-build"
PREFIX="${BASE_DIR}/toolchain/install"
EQY_SRC_DIR="${BASE_DIR}/toolchain/eqy-v0.69-src"
SBY_SRC_DIR="${BASE_DIR}/toolchain/sby-v0.69-src"
EXPECTED_COMMIT=9f75ca1f9834a39a863915b5dae0c7b1e33533bc

# This script is also the clean-machine bootstrap.  On an already prepared
# workspace, avoid a needless CMake reconfigure/reinstall; run_all.sh never
# builds tools and therefore has no network or compiler cost.
if [[ "${1:-}" != "--force" && -x "${PREFIX}/bin/yosys" && \
      -x "${PREFIX}/bin/eqy" && -x "${PREFIX}/bin/sby" ]]; then
    installed_yosys=$(${PREFIX}/bin/yosys -V)
    installed_eqy=$(${PREFIX}/bin/eqy --version)
    installed_sby=$(${PREFIX}/bin/sby --version)
    if [[ "${installed_yosys}" == *'Yosys 0.69 '* && \
          "${installed_yosys}" != *dirty* && "${installed_yosys}" != *post* && \
          "${installed_eqy}" == *'0.69'* && "${installed_sby}" == *'0.69'* ]]; then
        echo "Toolchain already present and version-checked; use --force to rebuild."
        exit 0
    fi
fi

if [[ ! -f "${SRC_DIR}/.gitcommit" ]]; then
    git -c core.autocrlf=false clone \
        --branch v0.69 --depth 1 --recurse-submodules --shallow-submodules \
        https://github.com/YosysHQ/yosys.git "${SRC_DIR}"
fi

if [[ "$(<"${SRC_DIR}/.gitcommit")" != "${EXPECTED_COMMIT}" && \
      "$(git -C "${SRC_DIR}" rev-parse HEAD 2>/dev/null || true)" != "${EXPECTED_COMMIT}" ]]; then
    echo "Yosys source is not the required v0.69 commit ${EXPECTED_COMMIT}" >&2
    exit 1
fi

cmake -S "${SRC_DIR}" -B "${BUILD_DIR}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${PREFIX}"
cmake --build "${BUILD_DIR}" --parallel "$(nproc)"
cmake --install "${BUILD_DIR}"

version=$(${PREFIX}/bin/yosys -V)
echo "${version}"
if [[ "${version}" != *'Yosys 0.69 '* || "${version}" == *dirty* || "${version}" == *post* ]]; then
    echo "Built Yosys does not report a pristine 0.69 version" >&2
    exit 1
fi

if [[ ! -d "${EQY_SRC_DIR}/.git" ]]; then
    git -c core.autocrlf=false clone \
        --branch v0.69 --depth 1 \
        https://github.com/YosysHQ/eqy.git "${EQY_SRC_DIR}"
fi

export PATH="${PREFIX}/bin:${PATH}"
make -C "${EQY_SRC_DIR}" -j "$(nproc)" YOSYS_CONFIG="${PREFIX}/bin/yosys-config"
make -C "${EQY_SRC_DIR}" PREFIX="${PREFIX}" YOSYS_CONFIG="${PREFIX}/bin/yosys-config" install

eqy_version=$(${PREFIX}/bin/eqy --version)
echo "${eqy_version}"
if [[ "${eqy_version}" != *'0.69'* ]]; then
    echo "Installed EQY does not report version 0.69" >&2
    exit 1
fi

if [[ ! -d "${SBY_SRC_DIR}/.git" ]]; then
    git -c core.autocrlf=false clone \
        --branch v0.69 --depth 1 \
        https://github.com/YosysHQ/sby.git "${SBY_SRC_DIR}"
fi
make -C "${SBY_SRC_DIR}" PREFIX="${PREFIX}" install
"${PREFIX}/bin/sby" --version

