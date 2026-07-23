#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
DOCKER="${DOCKER:-docker}"
DOCKER_IMAGE="${DOCKER_IMAGE:-cap-cc1101-nfc-packager:latest}"
DOCKER_PLATFORM="${DOCKER_PLATFORM:-linux/amd64}"
PARALLEL="${PARALLEL:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
CLEAN="${CLEAN:-0}"
REBUILD_IMAGE="${REBUILD_IMAGE:-0}"

usage() {
    cat <<EOF
Usage: $0 [--clean] [--rebuild-image]

  --clean          Reconfigure and rebuild the package from scratch.
  --rebuild-image  Rebuild the tool image without Docker layer cache.
EOF
}

while [[ "$#" -gt 0 ]]; do
    case "$1" in
        --clean)
            CLEAN=1
            ;;
        --rebuild-image)
            REBUILD_IMAGE=1
            ;;
        -h | --help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

if ! command -v "${DOCKER}" >/dev/null 2>&1; then
    echo "Docker CLI was not found. Install Docker or set DOCKER to its executable." >&2
    exit 1
fi

if ! "${DOCKER}" info >/dev/null 2>&1; then
    echo "Docker daemon is unavailable." >&2
    echo "On WSL2, enable this distribution under Docker Desktop > Settings > Resources > WSL Integration." >&2
    exit 1
fi

BUILD_ARGS=(
    build
    --platform "${DOCKER_PLATFORM}"
    --tag "${DOCKER_IMAGE}"
    --file "${SCRIPT_DIR}/Dockerfile"
)
if [[ "${REBUILD_IMAGE}" == "1" ]]; then
    BUILD_ARGS+=(--pull --no-cache)
fi
BUILD_ARGS+=("${SCRIPT_DIR}")
"${DOCKER}" "${BUILD_ARGS[@]}"

"${DOCKER}" run --rm --init \
    --platform "${DOCKER_PLATFORM}" \
    --user "$(id -u):$(id -g)" \
    --mount "type=bind,src=${ROOT_DIR},dst=/workspace" \
    --env HOME=/tmp/cap-nfc-home \
    --env "BSP_VERSION=${BSP_VERSION:-v0.0.4}" \
    --env "BSP_URL=${BSP_URL:-}" \
    --env "BSP_SHA256=${BSP_SHA256:-}" \
    --env "CLEAN=${CLEAN}" \
    --env "CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:-Release}" \
    --env "PACKAGE_SUFFIX=${PACKAGE_SUFFIX:-m5stack1}" \
    --env "PARALLEL=${PARALLEL}" \
    "${DOCKER_IMAGE}"
