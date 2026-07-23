#!/usr/bin/env bash
set -euo pipefail

HOME="${HOME:-/tmp/cap-nfc-home}"
ROOT_DIR="${ROOT_DIR:-/workspace}"
BSP_VERSION="${BSP_VERSION:-v0.0.4}"
BSP_URL="${BSP_URL:-https://github.com/CardputerZero/M5CardputerZero-UserDemo/releases/download/${BSP_VERSION}/sdk_bsp.tar.gz}"
BSP_SHA256="${BSP_SHA256:-}"
if [[ -z "${BSP_SHA256}" && "${BSP_VERSION}" == "v0.0.4" ]]; then
    BSP_SHA256="e51b6eb803ed08f450e459efbfe62dd0341440846f3be9d01da861fe6cfdebb0"
fi
BSP_CACHE_KEY="${BSP_VERSION//[^a-zA-Z0-9._-]/_}"
CACHE_DIR="${DOCKER_CACHE_DIR:-${ROOT_DIR}/build/docker-cache}"
ARCHIVE="${CACHE_DIR}/sdk_bsp-${BSP_CACHE_KEY}.tar.gz"
ARCHIVE_VALIDATION_STAMP="${ARCHIVE}.validated-sha256"
SYSROOT="${CACHE_DIR}/sdk_bsp-${BSP_CACHE_KEY}"
SYSROOT_VALIDATION_STAMP="${SYSROOT}/.cap-nfc-bsp-sha256"
OVERLAY_DIR="${SYSROOT_OVERLAY_DIR:-/opt/cap-nfc/sysroot-overlay}"
BUILD_DIR="${ROOT_DIR}/build/package-docker-cross-${BSP_CACHE_KEY}"
STAGE_DIR="${ROOT_DIR}/build/deb-root-docker-cross-${BSP_CACHE_KEY}"

if [[ ! -d "${ROOT_DIR}" || ! -f "${ROOT_DIR}/CMakeLists.txt" ]]; then
    echo "Cap-CC1101-NFC source is not mounted at ${ROOT_DIR}." >&2
    exit 1
fi

mkdir -p "${HOME}"
mkdir -p "${CACHE_DIR}"
if [[ ! -f "${ARCHIVE}" ]]; then
    echo "Downloading CardputerZero BSP ${BSP_VERSION} (cached after the first build)..."
    if ! curl --fail --location --retry 3 --continue-at - \
        --output "${ARCHIVE}.part" "${BSP_URL}"; then
        echo "Resuming the BSP download failed; retrying it from the beginning..." >&2
        rm -f "${ARCHIVE}.part"
        curl --fail --location --retry 3 --output "${ARCHIVE}.part" "${BSP_URL}"
    fi
    rm -f "${ARCHIVE_VALIDATION_STAMP}"
    mv "${ARCHIVE}.part" "${ARCHIVE}"
fi

if [[ -n "${BSP_SHA256}" ]]; then
    validated_sha256=""
    if [[ -f "${ARCHIVE_VALIDATION_STAMP}" ]]; then
        validated_sha256="$(<"${ARCHIVE_VALIDATION_STAMP}")"
    fi
    if [[ "${validated_sha256}" != "${BSP_SHA256}" || "${ARCHIVE}" -nt "${ARCHIVE_VALIDATION_STAMP}" ]]; then
        archive_sha256="$(sha256sum "${ARCHIVE}")"
        archive_sha256="${archive_sha256%% *}"
        if [[ "${archive_sha256}" != "${BSP_SHA256}" ]]; then
            echo "BSP checksum mismatch: expected ${BSP_SHA256}, got ${archive_sha256}." >&2
            echo "Remove ${ARCHIVE} and retry." >&2
            exit 1
        fi
        printf "%s\n" "${archive_sha256}" >"${ARCHIVE_VALIDATION_STAMP}"
    fi
fi

sysroot_sha256=""
if [[ -f "${SYSROOT_VALIDATION_STAMP}" ]]; then
    sysroot_sha256="$(<"${SYSROOT_VALIDATION_STAMP}")"
fi
if [[ ! -e "${SYSROOT}/usr/lib/aarch64-linux-gnu/libgpiod.so.3.1.1" || \
      (-n "${BSP_SHA256}" && "${sysroot_sha256}" != "${BSP_SHA256}") ]]; then
    echo "Preparing CardputerZero BSP sysroot..."
    EXTRACT_DIR="${SYSROOT}.extracting"
    rm -rf "${EXTRACT_DIR}" "${SYSROOT}"
    mkdir -p "${EXTRACT_DIR}"
    tar -xzf "${ARCHIVE}" -C "${EXTRACT_DIR}"

    if [[ ! -d "${EXTRACT_DIR}/usr/include" || ! -d "${EXTRACT_DIR}/usr/lib" ]]; then
        echo "Unexpected BSP archive layout: usr/include and usr/lib were not found." >&2
        exit 1
    fi
    if [[ ! -e "${EXTRACT_DIR}/usr/lib/aarch64-linux-gnu/libgpiod.so.3.1.1" ]]; then
        echo "The BSP does not contain the expected libgpiod.so.3.1.1 runtime." >&2
        exit 1
    fi

    if [[ -n "${BSP_SHA256}" ]]; then
        printf "%s\n" "${BSP_SHA256}" >"${EXTRACT_DIR}/.cap-nfc-bsp-sha256"
    fi
    mv "${EXTRACT_DIR}" "${SYSROOT}"
fi

if [[ ! -f "${OVERLAY_DIR}/usr/include/gpiod.h" ]]; then
    echo "The Docker sysroot overlay is missing gpiod.h." >&2
    exit 1
fi
if ! cmp -s "${OVERLAY_DIR}/usr/include/gpiod.h" "${SYSROOT}/usr/include/gpiod.h"; then
    install -D -m 644 "${OVERLAY_DIR}/usr/include/gpiod.h" "${SYSROOT}/usr/include/gpiod.h"
fi

reset_build="${CLEAN:-0}"
if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    cached_source="$(sed -n 's/^CMAKE_HOME_DIRECTORY:[^=]*=//p' "${BUILD_DIR}/CMakeCache.txt")"
    cached_sysroot="$(sed -n 's/^CMAKE_SYSROOT:[^=]*=//p' "${BUILD_DIR}/CMakeCache.txt")"
    if [[ "${cached_source}" != "${ROOT_DIR}" || "${cached_sysroot}" != "${SYSROOT}" ]]; then
        echo "The cached CMake paths belong to another workspace or sysroot; reconfiguring..."
        reset_build=1
    fi
fi
if [[ "${reset_build}" == "1" ]]; then
    echo "Removing cached package build directories..."
    rm -rf "${BUILD_DIR}" "${STAGE_DIR}"
fi

cd "${ROOT_DIR}"
python3 -c 'import fetch_repos; fetch_repos.ensure_dependencies()'

CAP_NFC_FORCE_CROSS=1 \
CAP_NFC_SYSROOT="${SYSROOT}" \
BUILD_DIR="${BUILD_DIR}" \
STAGE_DIR="${STAGE_DIR}" \
DIST_DIR="${ROOT_DIR}/dist" \
PARALLEL="${PARALLEL:-4}" \
CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}" \
PACKAGE_SUFFIX="${PACKAGE_SUFFIX:-m5stack1}" \
    "${ROOT_DIR}/packaging/deb/package_deb.sh"
