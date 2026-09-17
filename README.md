# Cap-CC1101-NFC

NFC tag reader for M5Stack CardputerZero and the ST25R3916 reader built into the Cap CC1101 accessory.

## Features

- Detect NFC-A tags (UID, ATQA, SAK) and NFC-F/FeliCa tags (IDm and tag type)
- Read Text and URI NDEF records from supported Type 2 tags
- Track tag presentation and removal for the current session
- Show reader, transport, IRQ, power, and protocol diagnostics
- Use an SDL mock reader for desktop development without NFC hardware

## Dependencies

Run the bootstrap script once after cloning this repository:

```bash
./bootstrap.sh
```

It fetches `lvgl`, `spdlog`, and `smooth_ui_toolkit` under `dependencies/`.
SDL builds require SDL2 development files. CardputerZero builds additionally
require libgpiod development headers and library.

## Build

For SDL desktop testing:

```bash
cmake -S . -B build/sdl -DCAP_NFC_USE_SDL=ON
cmake --build build/sdl -j8
```

For a native CardputerZero build:

```bash
cmake -S . -B build/cp0 -DCAP_NFC_USE_SDL=OFF
cmake --build build/cp0 -j8
```

The desktop and device binaries are written to `dist/sdl/` and `dist/device/`
respectively.

Run the tests with:

```bash
cmake -S . -B build/tests -DCAP_NFC_USE_SDL=ON -DBUILD_TESTING=ON
cmake --build build/tests -j8
ctest --test-dir build/tests --output-on-failure
```

## Usage

Run the SDL build with:

```bash
CAP_NFC_SDL_ZOOM=2 ./dist/sdl/M5CardputerZero-Cap-CC1101-NFC
```

Key controls:

- `Z`/`C` or Left/Right: switch between Scan, Tag Details, and Reader Info
- `F`/`X` or Up/Down: scroll details
- Enter: open tag details or retry reader initialization
- `R`: retry initialization from Reader Info
- Esc: return to Scan, close a dialog, or exit
- Fn+H (desktop: H): open or close Help; Esc also closes Help

Set `NFC_MOCK_SCENARIO` to `empty`, `text`, `uri`, or `cycle` to select the
desktop mock data.

## Hardware

The device build uses the Cap CC1101 ST25R3916 reader over `/dev/spidev0.2`,
with kernel-managed CS2 on GPIO22 and IRQ on GPIO23. Before reader
initialization, the app checks the SPI node and, when needed, loads the
BSP-provided `/boot/firmware/overlays/spi0-spidev2-gpio22-overlay.dtbo`. The
overlay remains loaded until reboot.

The Debian package launches this hardware app as root through a non-interactive,
command-specific sudo rule for members of the `gpio` group. This works with the
current APPLaunch behavior; the rule permits only the installed binary with no
command arguments.

The current BSP may fail to apply the overlay at runtime. If initialization
still reports a missing SPI node after loading, reboot once with
`dtoverlay=spi0-spidev2-gpio22-overlay` in `/boot/firmware/config.txt`.

The current implementation supports NFC-A UID discovery, NFC-F/FeliCa IDm
discovery (NFC Forum Type 3), and Type 2 NDEF reading on NFC-A. NFC-A Type 4
NDEF, NFC-B, and NFC-V discovery are not implemented.

## Package

Build the CardputerZero `arm64` Debian package on an x86 Linux or WSL2 host with
the Docker wrapper:

```bash
./packaging/docker/package_deb.sh
```

To package natively on a CardputerZero instead, run:

```bash
./packaging/deb/package_deb.sh
```

The generated package is written to `dist/`:

```text
dist/m5cardputerzero-cap-cc1101-nfc_<version>_m5stack1_arm64.deb
```

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the ST25R3916
algorithm reference and license.
