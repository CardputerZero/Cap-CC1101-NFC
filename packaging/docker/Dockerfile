FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
ARG LIBGPIOD_REF=v2.2.1
ARG LIBGPIOD_COMMIT=e2539f39118ba89e804451b0e60d3246b340326f

RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        binutils-aarch64-linux-gnu \
        ca-certificates \
        cmake \
        curl \
        dpkg-dev \
        g++-aarch64-linux-gnu \
        gcc-aarch64-linux-gnu \
        git \
        make \
        ninja-build \
        pkg-config \
        python3 \
    && rm -rf /var/lib/apt/lists/*

# The v0.0.4 BSP ships libgpiod.so.3.1.1 but omits its public header.
RUN git clone --depth 1 --branch "${LIBGPIOD_REF}" \
        https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git /tmp/libgpiod \
    && test "$(git -C /tmp/libgpiod rev-parse HEAD)" = "${LIBGPIOD_COMMIT}" \
    && install -D -m 644 /tmp/libgpiod/include/gpiod.h \
        /opt/cap-nfc/sysroot-overlay/usr/include/gpiod.h \
    && install -D -m 644 /tmp/libgpiod/COPYING \
        /opt/cap-nfc/licenses/libgpiod/COPYING \
    && install -D -m 644 /tmp/libgpiod/LICENSES/LGPL-2.1-or-later.txt \
        /opt/cap-nfc/licenses/libgpiod/LGPL-2.1-or-later.txt \
    && rm -rf /tmp/libgpiod

ENV CMAKE_GENERATOR=Ninja
WORKDIR /workspace
ENTRYPOINT ["/workspace/packaging/docker/build_package.sh"]
