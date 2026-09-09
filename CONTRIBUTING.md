# Contributing to E4-Black

E4 is an STM32F411 micromouse running a C++ port of the ukmars
**mazerunner-core** firmware, paired with a Web Bluetooth companion app.
This is a small private repo — this guide just keeps collaboration tidy.

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

No branch protection is set, so use judgement:

- **Small, safe changes** (a fix, a tweak, a doc edit) — commit straight to
  `main`. Just keep `main` buildable.
- **Anything larger, or that you'd like eyes on** — work on a short-lived
  branch and open a PR. Reviews aren't enforced, but a PR is a good place to
  discuss a change before it lands.
- Pull before you start; prefer clean, rebased history over merge noise where
  it's easy.

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
