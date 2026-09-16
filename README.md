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
- **Wall sensors:** 4× IR pairs — TSAL6100 emitters (940 nm) into OP505A
  phototransistors; emitters switched low-side by FDV301N MOSFETs from a 5 V
  rail. (The original board carried SFH4550; the sensor assemblies now fitted
  are TSAL6100.)
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
`docs/index.html`. Eleven chapters: overview, architecture & pin map, sensor
aim, sensor geometry, sensor housing, the firmware/menu map, position integrity
("Staying Located"), a design note for the next board (STM32G431), how she
chooses her path, the turn table, and the sensor carrier.

## Repository layout

| Path | What it is |
|------|-----------|
| `Source/` | STM32CubeIDE firmware project (Core / Drivers / Modules / Program). |
| `e4-maze.html` | Web Bluetooth companion app (single file) — the competition tool. |
| `CompanionApp/` | Native SwiftUI companion (macOS/iPad) + the `E4Core` package. |
| `docs/` | The reference manual (`index.html` + chapters). |
| `3D Files/` | Sensor housings, wheels, mounts, and the sensor socket plate (STL, print notes, and the parametric source that generates and verifies it). |
| `tools/planner-bench/` | Native build of the route planner, for scoring routes against the maze files without a robot. |
| `mazefiles/` | 408 real competition mazes, binary and text. |
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

---

## Firmware releases

`FW_VERSION` lives in `Source/Program/inc/version.h`, shows on the OLED splash
and is reported over Bluetooth (`VER?`). **Minor** for a feature, **patch** for
a fix. **1.0.0 is reserved** for the build that goes to the October competition
— everything before it is bring-up.

Each entry names the firmware commit; companion-app and docs commits that
landed alongside are listed after it, since the two move together. Newest
first.

**0.19 — 16 Sep 2026 · A route planner, and a test to prove it wrong**
Two new modules that nothing drives yet, and one bench routine that exists to
settle an argument between them and the firmware.

`planner.cpp` answers *least time* where the flood in `maze.h` answers *fewest
cells* — a different graph, not a replacement. Time is not additive per cell
(she accelerates, so four one-cell moves and one four-cell straight differ), so
nodes are turns rather than cells and an edge is "run N cells, then turn". The
FIFO had to go with it: a plain queue is correct only while every edge costs
the same, which is what unit cost bought. It is Dial's algorithm now — a
circular bucket queue, still O(E), with a guard that invalidates the plan
rather than clamping if an edge outgrows the ring.

`diagonal.cpp` does classic diagonal substitution on the result, costing every
candidate both ways and substituting only when the diagonal actually wins.
**Worth 3.4%** across 392 real competition mazes. An earlier version of this
entry claimed 8–10%; that figure came from routes she could not have driven,
and is corrected in 0.20.

**Zigzag test** (Calibration, or `Z`) drives N alternating 90s in consecutive
cells with no straight between, ending stopped at a cell centre so the offset
can be measured. It is there because the planner and `turn_smooth()` disagree:
at R = v/ω ≈ 101 mm two arcs one cell apart need 202 mm and have 180, so the
planner refuses and emits stop-and-spin — while `turn_smooth()` relabels the
frame past the next turn point and simply runs the arcs back to back. `ZIG,` over
BT sets turns, mode (chained arcs or spins) and first direction. Whichever way
it comes out, one of the two is wrong and the 8–10% diagonal figure moves with it.

Neither planner module is called by anything; `--gc-sections` drops all 36 KB
of the planner's working set until something does.

**0.18 — 13 Sep 2026 · Connecting greets her, not drives her**
The app asked for the firmware version by *running the menu action*, which
parks on the OLED waiting for a button press — on a screen nearly identical to
the boot splash, so she read as hung, and the menu cursor was left sitting in
Diagnostics. Replaced with a `VER?` query that answers without touching the
menu.
`f9c32fd` · tests `9a5f6d8`

**0.17 — 13 Sep 2026 · The whole configuration, on demand**
`CFG?` dumps every persisted value — gyro scale, the three wall thresholds, all
sixteen turn rows and the spin dynamics — so the app can show what she is
actually running rather than what it last sent her.
`246aeec`

**0.16 — 13 Sep 2026 · Spins join the table**
`OMEGA_SPIN_TURN` / `ALPHA_SPIN_TURN` become runtime values: tunable over
Bluetooth (`SPIN,…`) and saved with everything else. Config block goes to v5.
`9c71672`

**0.15 — 13 Sep 2026 · Slots for the diagonal set**
The turn table grows from four rows to the full sixteen — SS90E, SS90, SS180,
and the diagonal entries and exits (SD45, DS45, SD135, DS135, DD90), left and
right. The diagonal rows carry computed geometry and sit inert until the
diagonal solver exists.
`6bc5987` · docs `d1c8ef4` (Ch10, the turn table)

**0.14 — 13 Sep 2026 · One turn, tunable live, saved to EEPROM**
There were three separate copies of the same 90° turn and they had already
drifted apart — the menu ran alpha 1000 where the search brain ran 2500.
Collapsed into a single `turn_params[]` table that the menu, the search brain
and the tuner all read from, editable over Bluetooth and persisted.
`4f0f4d1`

**0.13 — 13 Sep 2026 · Show what is running, on both displays**
Commands sent from the app bypassed the menu, so the OLED carried on showing
something else entirely. The menu cursor now follows whatever action actually
runs, wherever it was started from.
`d339d11` · app `f2d5cd0`, `11cd734`, `b323651`, `b828179`, `108e0c8`, `382c723`
(threshold calibration drawn rather than described; session replay)

**0.12 — 13 Sep 2026 · Threshold calibration: the third capture**
Two captures — walls present, walls absent — cannot describe a corridor, where
the sides see wall and the front does not, which is the state she spends most
of her life in. Added a third capture and generalised the margin test to
weakest-present against strongest-absent.
`93a3c9c` · app `ee8095a`, `02bf440`, `167dc30` (maze file import and upload)

**0.11 — 12 Sep 2026 · Per-sensor wall thresholds, calibrated and persisted**
One shared side threshold became `WALL_THRESH_LEFT` / `_RIGHT` / `_FRONT`,
settable over Bluetooth (`THR,l,r,f`), captured by a guided routine and saved
to EEPROM. Also fixed the EEPROM self-test, which had been writing its scratch
pattern to address 0 — over the maze store.
`12142da` · app `3dbdb43` · docs `7b189fa` (Ch9, how she chooses her path)

**0.10 — 11 Sep 2026 · One-press Bluetooth bring-up**
A replacement module can be provisioned from the menu — name set to `MMOUSE`,
baud to 57600 — with no serial adapter and no guesswork.
`8c0b13a` · app `64b5e6f`, `2ce5fc9`, `2a243a9`, `29ccaab` (the native SwiftUI
client starts here: sidebar, seven screens, session logging)

**0.9 — 11 Sep 2026 · Gyro scale becomes a runtime value**
`GYRO_SCALE` moves out of the source and into the config store: spin a known
angle, send the error, save. It is a property of the individual MPU-9250 and
shifts with temperature, so it has to be re-measurable at a venue with nothing
but the app.
`f03e0b8`

**0.8 — 10 Sep 2026 · Non-blocking IR sampler**
A five-state sampler ticked from the control ISR returns a full
ambient-subtracted set every 5 ms with no busy-wait, so the wall flags stay
live while she is moving.
`1498a93` · app `daa2db3`, `2e7a50a` · repo `2fcd708` (CODEOWNERS), `1c77a9d`

**0.7 — 9 Sep 2026 · Live turn tuning over Bluetooth**
`act_turn_tune` stops being a stub: parametric in-place spins and arcs run on
command and stream back the achieved gyro angle and forward distance, so turn
dynamics can be tuned against the surface she is actually on without
reflashing.
`3ae1b6e` · app `910cb1b` · docs `e2a6766` (Ch7), `381987e` (Ch8)

**0.6 — 8 Sep 2026 · Batched IR read, ADC timing, menu tools**
Two-phase batched wall read (dark, then per-emitter lit) and IR sample time
raised from 3 to 28 cycles. First release with a stamped version.
`63e09fa` · app `2f60ad7` · docs `c0be1df` (the manual moves into the repo)
