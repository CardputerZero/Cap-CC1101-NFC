#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"
PYTHON_ENV_BIN=""

for candidate in "${ROOT_DIR}/.venv/bin/python" "${ROOT_DIR}/../.venv/bin/python"; do
    if [[ -x "${candidate}" ]] && "${candidate}" -m pip --version >/dev/null 2>&1; then
        PYTHON_ENV_BIN="${candidate}"
        break
    fi
done

if [[ -z "${PYTHON_ENV_BIN}" ]]; then
    "${PYTHON_BIN}" -m venv "${ROOT_DIR}/.venv"
    PYTHON_ENV_BIN="${ROOT_DIR}/.venv/bin/python"
fi

if ! "${PYTHON_ENV_BIN}" -m pip --version >/dev/null 2>&1; then
    echo "Python environment is missing pip. Install python3-venv and retry." >&2
    exit 1
fi

if [[ ! -d "${ROOT_DIR}/dependencies/lvgl/.git" || \
      ! -d "${ROOT_DIR}/dependencies/spdlog/.git" || \
      ! -d "${ROOT_DIR}/dependencies/smooth_ui_toolkit/.git" ]]; then
    "${PYTHON_ENV_BIN}" "${ROOT_DIR}/fetch_repos.py"
fi

echo "Cap-CC1101-NFC bootstrap complete."
echo "Build the SDL version with:"
echo "  cmake -S . -B build/sdl -DCAP_NFC_USE_SDL=ON"
echo "  cmake --build build/sdl -j$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
