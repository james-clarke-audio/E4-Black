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
  position and wall presence. Gyro scale is **calibrated on the floor and
  persisted to EEPROM**, not compiled in — it is a property of the individual
  MPU-9250 and shifts with temperature, so it can be re-measured at a venue
  with nothing but the companion app.
- **On-board UI** — SSD1306 OLED with a two-button menu; the whole menu is also
  drivable over Bluetooth.
- **Bluetooth telemetry & control** via the companion app — a Control tab for
  watching and driving her, and a Tuning tab for setting her up: live IR
  monitor, an emitter-hold camera aid for aiming, live turn tuning (gyro-closed
  spins & arcs), and firmware-version reporting.
- **Non-blocking wall sampling** (opt-in) — a five-state sampler ticked from the
  control ISR returns a full ambient-subtracted set every 5 ms with no
  busy-wait, so the wall flags stay live while she's moving.
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
- **Hosted copy:** the repo publishes to GitHub Pages, so the current app is
  always live at
  [`/E4-Black/e4-maze.html`](https://james-clarke-audio.github.io/E4-Black/e4-maze.html)
  and the manual at [`/E4-Black/docs/`](https://james-clarke-audio.github.io/E4-Black/docs/).
  Web Bluetooth needs a secure context, so on a tablet this is the route to use
  rather than copying the file across.

## Documentation

The **E4 Reference Manual** lives in [`docs/`](docs/index.html) — open
`docs/index.html`. Eight chapters: overview, architecture & pin map, sensor aim,
sensor geometry, sensor housing, the firmware/menu map, position integrity
("Staying Located"), and a design note for the next board (STM32G431).

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

`main` is protected: collaborators work on a branch and open a pull request.
Details, commit style and the firmware-version convention are in
[CONTRIBUTING.md](CONTRIBUTING.md).

## Credits

Firmware is a port of Peter Harrison's ukmars **mazerunner-core**. Thanks to the
UK micromouse community.

## License

Released under the [MIT License](LICENSE) — © 2022 Peter Harrison
(ukmars mazerunner-core), © 2026 James Clarke. E4 is a derivative of the
MIT-licensed mazerunner-core and keeps the same terms.
