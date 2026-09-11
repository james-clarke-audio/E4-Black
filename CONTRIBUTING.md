# Contributing to E4-Black

E4 is an STM32F411 micromouse running a C++ port of the ukmars
**mazerunner-core** firmware, paired with a Web Bluetooth companion app.
The repo is public and `main` is protected — this guide keeps collaboration
tidy and says how changes get in.

## Repository layout

| Path | What it is |
|------|-----------|
| `Source/` | STM32CubeIDE project (the firmware). `Core/` = HAL, `Drivers/`, `Modules/` = drivers (IRS, EEPROM, OLED…), `Program/` = the brain (mouse, maze, control, robot_sensors, app_main). |
| `e4-maze.html` | Web Bluetooth companion app — single self-contained file. |
| `docs/` | The E4 Reference Manual. Open `docs/index.html`. |
| `3D Files/` | Sensor housings, wheels, mounts (STL / GX). |
| `E4-cell-alignment-A4.pdf` | 1:1 print jig for squaring a test cell. |
| `mazefiles/`, `documents/` | Reference material. |

## Toolchain

- **Firmware:** STM32CubeIDE (2.2 / GCC 14). Import `Source/` as an existing
  project, build the **Debug** config, flash over ST-Link. Target is the
  **STM32F411CEU6**.
- **App:** any Web Bluetooth browser — Chrome/Edge on desktop, or an iPad via a
  WebBLE browser such as Bluefy (iOS Safari has no Web Bluetooth). Just open
  `e4-maze.html`.

## Workflow

`main` is protected by a branch rule, so **changes reach it through a pull
request**:

- **Branch first.** Short-lived branch off an up-to-date `main`, named for what
  it does (`feature/turn-tune`, `fix/adc-timing`).
- **Open a PR.** It's the only route in for collaborators — a direct push to
  `main` will be rejected.
- **Get it approved.** `CODEOWNERS` makes @james-clarke-audio the owner of the
  whole tree and the rule requires a code-owner review, so every PR needs an
  approval from him — one collaborator approving another doesn't satisfy it.
- **Keep `main` buildable.** Don't merge anything that doesn't compile in
  CubeIDE's Debug config.
- Pull before you start; prefer clean, rebased history over merge noise where
  it's easy.

The repo owner (@james-clarke-audio) has admin bypass and may push small doc or
config fixes straight to `main`. That's the exception, not the pattern — and
it's worth knowing that a branch whose commits reached `main` via a PR merge
plus a cherry-pick won't `git branch -d` (the SHAs differ); use `-D`, the
content is safe.

## Commits

- Keep them **clean and staged** — one logical change per commit, not a
  catch-all.
- Message style: a short `Area: summary` subject, then bullets for detail:

  ```
  Firmware: batched IR read, ADC timing
  - two-phase batched wall read (dark then per-emitter lit)
  - raise IR sample time 3 -> 28 cycles
  ```

- Don't commit generated or local files — these are gitignored and should stay
  that way: build output (`Source/Debug/`), editor backups (`*.bak*`), and the
  `Arduino Reference/` upstream (kept locally, not tracked).

## Firmware version

`Source/Program/inc/version.h` holds `FW_VERSION` — shown on the OLED and
streamed as `VER` over Bluetooth. Bump it with functional changes: **minor**
for a feature (`0.6` → `0.7`), **patch** for a fix/tweak (`0.6` → `0.6.1`). The
build date/time stamp is automatic.

## Firmware conventions

- Mixed C/C++ on STM32 HAL, with a **1 kHz** SysTick control loop.
- Menu actions live in a flat `MENU[]` registry in `Program/src/app_main.cpp`,
  grouped by `CAT[]` / `MODE[]`. Add an action there and wire it into a category.
- **Calibrated values live in the mouse, not in the source.** `config_store`
  owns a versioned block in the EEPROM (address 512, clear of the maze store):
  magic, version, length, payload, checksum, written then read back. Gyro scale
  is there now and the wall thresholds are next. To add a field, widen the
  payload and bump `CONFIG_VERSION` — an older block still loads and the new
  field takes its compiled default. A board with no EEPROM is not an error: the
  defaults stand and saving reports failure.
- Wall sensing is in `Modules` (IRS) + `Program/inc/robot_sensors.h`. Keep pin
  assignments in sync with `Source/E4-Black.ioc` (CubeMX) — the `.ioc` is the
  source of truth for pins, and regenerating from it must not clobber
  hand-written logic.

## Docs

The reference manual is the numbered set under `docs/` (start at
`docs/index.html`). It's a browsable, offline copy — if you edit a chapter, keep
the chapter-to-chapter nav links **relative** so it stays offline-friendly.

## Questions

It's a small team — just ask **@james-clarke-audio**.
