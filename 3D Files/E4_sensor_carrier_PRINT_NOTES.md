# E4 one-piece sensor carrier — v3

Generated from `documents/schematics/E4-blackpill-v2.brd`. Board frame, mm, **z = 0 is the PCB top surface**.

## Orientation and supports

Print **baseplate-underside down** — the flat face at z = 2.0 in the model is the bed contact.
The two rim feet hang 5.8 mm below that plane, so raise the part and **support under the two feet only**.
Everything else is self-supporting: genuine overhang area is **0.0 mm²**.

## Slicer

- **≥ 4 perimeters** and **solid infill through the sensor blocks.** Black PLA blocks 940 nm well,
  but the FDM leak path is light piping along layer boundaries, and the septum between an
  emitter and its detector is thin.
- 0.4 mm nozzle, 0.15–0.20 mm layers. No brim needed; the bed contact is large.

## Optics

Emitter and detector are solved **independently** and aimed at a **common target point**, because
their leads differ 2:1 (TSAL6100 26.5 mm, OP505A 12.7 mm). They are not parallel and are not
at the same height — that is deliberate.

| ch | part | optic (x, y, z) | azimuth | elevation | to target | lead used |
|----|------|-----------------|---------|-----------|-----------|-----------|
| SL | TSAL6100 | (12.50, 83.25, 12.0) | +63.6° | 0.00° | 64.7 mm | 24.4 / 26.5 |
| SL | OP505A   | (17.25, 89.25,  6.0) | +70.1° | +5.14° | 66.7 mm | 10.7 / 12.7 |
| SR | TSAL6100 | (64.50, 83.25, 12.0) | −63.6° | 0.00° | 64.7 mm | 24.5 / 26.5 |
| SR | OP505A   | (59.75, 89.25,  6.0) | −70.1° | +5.14° | 66.7 mm | 10.5 / 12.7 |
| FL | TSAL6100 | ( 6.75, 73.50, 12.0) | +10.3° | 0.00° | — | 16.8 / 26.5 |
| FL | OP505A   | ( 5.50, 73.00,  6.0) | +10.2° | +0.87° | — |  9.3 / 12.7 |
| FR | TSAL6100 | (70.00, 74.25, 12.0) | −10.4° | 0.00° | — | 16.6 / 26.5 |
| FR | OP505A   | (71.25, 73.00,  6.0) | −10.2° | +0.87° | — |  9.3 / 12.7 |

Side target: the flat wall face at y = 112, which is **7 mm clear of the forward post**.
Round trip 81.1 / 81.1 mm → 64.7 / 66.7 mm, about **×1.53** the return.
Emitter floor-tilt margin **10.5°** (was 4.9°); the detector is tilted 5.1° *away* from the floor.

## V-troughs

90° included angle, self-centring. Apex depths are set from the **barrel** diameters —
TSAL6100 ø5.00, OP505A ø3.05 (**not** the ø3.94 flange). A V locates a cylinder whatever the
print tolerance, so the aim comes from the trough direction, not from a hole diameter.

## Fitting

1. Print, clean the troughs, test-fit a part in each. It should seat with light thumb pressure.
2. Bolt the carrier down: apex **M2 at (38.5, 98)**, nut under the board. The two rim feet
   hook the board edge; each has a **1.6 × 0.6 mm relief groove** so it seats on bare solder
   resist and not on the 5 V trace that runs under the rim.
3. Drop each part into its trough, dome forward.
4. Form and trim the leads to the pads, then solder. The joints carry no aim.
5. **Dab of epoxy in the well behind each part's rear flange** — the trough is widened for the
   last 3 mm precisely for this. Rear only: the V sets the aim, the epoxy only retains, and
   nothing goes near the optics.

## Clearances

Minimum carrier-to-component clearance **0.90 mm**, over Q1/Q3 (SOT-23). Neither the Bluetooth
header nor the Black Pill is touched at any height.
