# E4 — "E4-Black"

An **STM32F411 micromouse**: a C++ port of the ukmars
[**mazerunner-core**](https://github.com/ukmars/mazerunner-core) firmware onto
custom hardware, with a **Web Bluetooth** companion app for live telemetry,
control and calibration. Built for the October 2026 micromouse competition.

> **Status:** active development — hardware bring-up and sensor calibration.

## Features

- **1 kHz control loop** (SysTick) — motion, sensing and the search brain all
  run deterministically.
- **IR wall sensing** — four emitter/detector pairs, ambient-subtracted with a
  batched dark-then-lit read. Two forward sensors (front wall) and two diagonal
  sensors (side walls / centring).
- **Gyro heading** — the gyro holds heading; the side sensors handle lateral
  position and wall presence.
- **On-board UI** — SSD1306 OLED with a two-button menu; the whole menu is also
  drivable over Bluetooth.
- **Bluetooth telemetry & control** via the companion app — live IR monitor, an
  emitter-hold camera aid for aiming, and firmware-version reporting.
- **Maze persistence** to on-board EEPROM.
- **Flood-fill search / speed run** from mazerunner-core, with a virtual/real
  sensor switch so the brain can be exercised in simulation.

## Hardware

- **MCU:** STM32F411CEU6 (Cortex-M4F, "Black Pill"-class).
- **Wall sensors:** 4× IR pairs — SFH4550 emitters (migrating to TSAL6100,
  940 nm) into OP505A phototransistors; emitters switched low-side by FDV301N
  MOSFETs from a 5 V rail.
- **Display / storage:** SSD1306 OLED and a 24LC256 EEPROM, both on I2C1.
- **Drive:** DRV8833 motor driver, plus a gyro for heading.

Full pin map, sensor geometry and housing detail are in the reference manual
(see below).

## Build & run

- **Firmware:** STM32CubeIDE (2.2 / GCC 14). Import `Source/` as an existing
  project, build the **Debug** config, and flash over ST-Link to the
  STM32F411CEU6.
- **Companion app:** open `e4-maze.html` in a Web Bluetooth browser — Chrome or
  Edge on desktop, or an iPad via a WebBLE browser such as Bluefy (iOS Safari
  has no Web Bluetooth). Pair over BLE to monitor the sensors, drive the menu,
  and read the running firmware version.

## Documentation

The **E4 Reference Manual** lives in [`docs/`](docs/index.html) — open
`docs/index.html`. Six chapters: overview, architecture & pin map, sensor aim,
sensor geometry, sensor housing, and the firmware/menu map.

## Repository layout

| Path | What it is |
|------|-----------|
| `Source/` | STM32CubeIDE firmware project (Core / Drivers / Modules / Program). |
| `e4-maze.html` | Web Bluetooth companion app (single file). |
| `docs/` | The reference manual (`index.html` + chapters). |
| `3D Files/` | Sensor housings, wheels, mounts. |
| `E4-cell-alignment-A4.pdf` | 1:1 print jig for squaring a test cell. |

See [CONTRIBUTING.md](CONTRIBUTING.md) for the fuller layout and workflow.

## Contributing

It's a small private repo — commit small changes straight to `main`, branch and
open a PR for anything larger. Details, commit style and the firmware-version
convention are in [CONTRIBUTING.md](CONTRIBUTING.md).

## Credits

Firmware is a port of Peter Harrison's ukmars **mazerunner-core**. Thanks to the
UK micromouse community.

## License

Released under the [MIT License](LICENSE) — © 2022 Peter Harrison
(ukmars mazerunner-core), © 2026 James Clarke. E4 is a derivative of the
MIT-licensed mazerunner-core and keeps the same terms.
