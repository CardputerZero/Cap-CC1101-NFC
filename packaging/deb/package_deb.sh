#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PACKAGE_NAME="${PACKAGE_NAME:-m5cardputerzero-cap-cc1101-nfc}"
PACKAGE_SUFFIX="${PACKAGE_SUFFIX:-m5stack1}"
DEB_ARCH="arm64"
MAINTAINER="${MAINTAINER:-m5stack <m5stack@m5stack.com>}"
PARALLEL="${PARALLEL:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/package}"
STAGE_DIR="${STAGE_DIR:-${ROOT_DIR}/build/deb-root}"
DIST_DIR="${DIST_DIR:-${ROOT_DIR}/dist}"
BIN_NAME="M5CardputerZero-Cap-CC1101-NFC"
CMAKE_BIN="${CMAKE:-cmake}"
CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
CAP_NFC_SYSROOT="${CAP_NFC_SYSROOT:-}"
CAP_NFC_FORCE_CROSS="${CAP_NFC_FORCE_CROSS:-0}"
READELF_BIN="${READELF:-readelf}"

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Required command not found: $1" >&2
        exit 1
    fi
}

read_cmake_cache_value() {
    local name="$1"
    local cache_file="${BUILD_DIR}/CMakeCache.txt"
    local line=""
    line="$(grep -E "^${name}(:[^=]*)?=" "${cache_file}" | tail -n 1 || true)"
    if [[ -z "${line}" ]]; then
        echo "CMake cache value not found: ${name}" >&2
        return 1
    fi
    printf "%s\n" "${line#*=}"
}

CMAKE_CONFIGURE_ARGS=(
    -S "${ROOT_DIR}"
    -B "${BUILD_DIR}"
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
    -DCAP_NFC_BIN_NAME="${BIN_NAME}"
    -DCAP_NFC_USE_SDL=OFF
    -DCAP_NFC_OUTPUT_DIR="${BUILD_DIR}/dist"
)

host_arch="$(uname -m)"
if [[ "${CAP_NFC_FORCE_CROSS}" == "1" || ("${host_arch}" != "aarch64" && "${host_arch}" != "arm64") ]]; then
    if [[ -z "${CAP_NFC_SYSROOT}" ]]; then
        echo "Cross-packaging Cap-CC1101-NFC requires CAP_NFC_SYSROOT with AArch64 libgpiod development files." >&2
        echo "Build this package natively on CardputerZero, or provide a complete target sysroot." >&2
        exit 1
    fi
    for compiler in aarch64-linux-gnu-gcc aarch64-linux-gnu-g++; do
        require_command "${compiler}"
    done
    READELF_BIN="${READELF:-aarch64-linux-gnu-readelf}"
    export PKG_CONFIG_SYSROOT_DIR="${CAP_NFC_SYSROOT}"
    export PKG_CONFIG_LIBDIR="${CAP_NFC_SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig:${CAP_NFC_SYSROOT}/usr/lib/pkgconfig:${CAP_NFC_SYSROOT}/usr/share/pkgconfig"
    export PKG_CONFIG_PATH=""
    CMAKE_CONFIGURE_ARGS+=(-DCMAKE_TOOLCHAIN_FILE="${ROOT_DIR}/cmake/aarch64-linux-gnu.cmake")
    CMAKE_CONFIGURE_ARGS+=(-DCMAKE_SYSROOT="${CAP_NFC_SYSROOT}")
fi

for command in "${CMAKE_BIN}" "${READELF_BIN}" dpkg-deb; do
    require_command "${command}"
done

"${CMAKE_BIN}" "${CMAKE_CONFIGURE_ARGS[@]}"
if [[ "$(read_cmake_cache_value CAP_NFC_USE_SDL)" != "OFF" ]]; then
    echo "Invalid package build: CAP_NFC_USE_SDL must be OFF." >&2
    exit 1
fi
PACKAGE_VERSION="$(read_cmake_cache_value CMAKE_PROJECT_VERSION)"
"${CMAKE_BIN}" --build "${BUILD_DIR}" -j"${PARALLEL}"

EXECUTABLE="${BUILD_DIR}/dist/${BIN_NAME}"
DESKTOP_TEMPLATE="${SCRIPT_DIR}/cap-cc1101-nfc.desktop.in"
SUDOERS_FILE="${SCRIPT_DIR}/m5cardputerzero-cap-cc1101-nfc.sudoers"
ICON_FILE="${SCRIPT_DIR}/images/cap-cc1101-nfc.png"
LICENSE_FILE="${ROOT_DIR}/LICENSE"
THIRD_PARTY_NOTICES_FILE="${ROOT_DIR}/THIRD_PARTY_NOTICES.md"
for path in "${EXECUTABLE}" "${DESKTOP_TEMPLATE}" "${SUDOERS_FILE}" "${ICON_FILE}" "${LICENSE_FILE}" \
    "${THIRD_PARTY_NOTICES_FILE}"; do
    if [[ ! -f "${path}" ]]; then
        echo "Required file not found: ${path}" >&2
        exit 1
    fi
done

machine="$(${READELF_BIN} -h "${EXECUTABLE}" | awk -F: '/Machine:/ { sub(/^[[:space:]]+/, "", $2); print $2; exit }')"
if [[ "${machine}" != "AArch64" ]]; then
    echo "Invalid package executable architecture: expected AArch64, got ${machine:-unknown}." >&2
    exit 1
fi

dynamic_section="$(${READELF_BIN} -d "${EXECUTABLE}")"
if [[ "${dynamic_section}" == *"libSDL2"* ]]; then
    echo "Invalid package executable: SDL must not be linked in a device build." >&2
    exit 1
fi

mapfile -t gpiod_sonames < <(printf "%s\n" "${dynamic_section}" | sed -n 's/.*Shared library: \[\(libgpiod\.so\.[^]]*\)\].*/\1/p')
if [[ "${#gpiod_sonames[@]}" -ne 1 ]]; then
    echo "Invalid package executable: expected one libgpiod dependency, found ${#gpiod_sonames[@]}." >&2
    exit 1
fi

case "${gpiod_sonames[0]}" in
    libgpiod.so.2)
        GPIOD_PACKAGE_DEPENDENCY="libgpiod2"
        ;;
    libgpiod.so.3)
        GPIOD_PACKAGE_DEPENDENCY="libgpiod3"
        ;;
    *)
        echo "Unsupported libgpiod SONAME: ${gpiod_sonames[0]}" >&2
        exit 1
        ;;
esac

rm -rf "${STAGE_DIR}"
mkdir -p "${STAGE_DIR}/DEBIAN" "${STAGE_DIR}/etc/sudoers.d" "${STAGE_DIR}/usr/share/APPLaunch/bin" \
    "${STAGE_DIR}/usr/share/APPLaunch/applications" \
    "${STAGE_DIR}/usr/share/APPLaunch/share/images" \
    "${STAGE_DIR}/usr/share/doc/${PACKAGE_NAME}" "${DIST_DIR}"
install -m 755 "${EXECUTABLE}" "${DIST_DIR}/${BIN_NAME}"
install -m 755 "${EXECUTABLE}" "${STAGE_DIR}/usr/share/APPLaunch/bin/${BIN_NAME}"
install -m 644 "${DESKTOP_TEMPLATE}" \
    "${STAGE_DIR}/usr/share/APPLaunch/applications/cap-cc1101-nfc.desktop"
install -m 440 "${SUDOERS_FILE}" \
    "${STAGE_DIR}/etc/sudoers.d/m5cardputerzero-cap-cc1101-nfc"
install -m 644 "${ICON_FILE}" \
    "${STAGE_DIR}/usr/share/APPLaunch/share/images/cap-cc1101-nfc.png"
install -m 644 "${LICENSE_FILE}" "${STAGE_DIR}/usr/share/doc/${PACKAGE_NAME}/LICENSE"
install -m 644 "${THIRD_PARTY_NOTICES_FILE}" \
    "${STAGE_DIR}/usr/share/doc/${PACKAGE_NAME}/THIRD_PARTY_NOTICES.md"

INSTALLED_SIZE="$(du -sk "${STAGE_DIR}/usr" | awk '{print $1}')"
cat >"${STAGE_DIR}/DEBIAN/control" <<EOF
Package: ${PACKAGE_NAME}
Version: ${PACKAGE_VERSION}
Section: utils
Priority: optional
Architecture: ${DEB_ARCH}
Maintainer: ${MAINTAINER}
Depends: libc6, libstdc++6, libgcc-s1, ${GPIOD_PACKAGE_DEPENDENCY}, raspi-utils-dt, sudo
Installed-Size: ${INSTALLED_SIZE}
Description: Cap CC1101 NFC reader application for M5CardputerZero APPLaunch
 Runtime NFC-A tag reader for the ST25R3916 on the Cap CC1101 accessory.
EOF

DEB_PATH="${DIST_DIR}/${PACKAGE_NAME}_${PACKAGE_VERSION}_${PACKAGE_SUFFIX}_${DEB_ARCH}.deb"
dpkg-deb --build --root-owner-group "${STAGE_DIR}" "${DEB_PATH}"
if [[ "$(dpkg-deb -f "${DEB_PATH}" Architecture)" != "${DEB_ARCH}" ]]; then
    echo "Generated package has an invalid architecture field." >&2
    exit 1
fi
echo "Generated Debian package: ${DEB_PATH}"
