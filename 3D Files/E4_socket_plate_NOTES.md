# E4 sensor socket plate — v8

Holds four of your existing `Sensor_Housing.STL` parts. The housing drops through the plate and
**seats on the PCB**, so the housing sets the optics and the plate only sets position and azimuth.

Detector optic **7.021 mm** above the seat, emitter **12.743 mm** — emitter on top, 5.72 apart.
Footprint 6.893 across × 8.025 along the beam.

## Placement (70° on the sides)

| ch | front face | azim | emitter lead | detector lead |
|----|-----------|------|--------------|---------------|
| SL | (13.50, 91.41) | +70.0° | 19.5 / 26.5 | 12.2 / 12.7 |
| SR | (62.69, 92.23) | -69.5° | 19.0 / 26.5 | 10.7 / 12.7 |
| FL | (4.84, 76.75) | +9.5° | 18.7 / 26.5 | 10.9 / 12.7 |
| FR | (72.13, 76.73) | -10.5° | 18.7 / 26.5 | 10.9 / 12.7 |

Side reach **62.8 mm** against 81.1 as the board is routed — about **×1.67** on the emitter leg —
landing at y = 112.9, **6 mm clear of the forward post**. 70° is the shallowest angle that clears
the post: 60° still lands on it, 65° catches its edge. Change `abs(az)>70.0` in the source to move it.

## Verified

| check | result |
|---|---|
| Beam cones — 33 rays per optic, against the plate **and all four placed housings** | **0 of 264 blocked** |
| Lead paths — every pad reachable from the board up to its lead height | **0 of 16 blocked** |
| Pocket walls — both flanks, full 8 mm length | **10/10 on all four** |
| Black Pill / BT header / SPI-I2C board | **no intersection** |
| Mesh | watertight, 1 body, 10 g, 84 × 48 × 12 mm |
| Narrowest neck | **2.4 mm** (was 0.8 mm at 3 mm thick) |
| Section at mid-height | 3 connected regions (was 37) |
| Genuine overhang | **0.0 mm²** |

The pockets are U-channels: both flanks full length (that is what fixes the azimuth), open at the
front for the beam and at the rear for the leads.

## The Black Pill

Its real library footprint is **20.32 × 52.07, offset and rotated 90°** — world X 12.7…64.8,
Y 57.2…77.5. Earlier versions of this plate sat straight through it, because I had checked against
a guessed centred box instead of the library. The plate's inner radius is now 31.6 mm and the real
keep-out volumes for the Black Pill and the BT header are subtracted from the solid.

## Plate thickness

The plate is **6.4 mm** thick (z = 2.0 to 8.4), not 3 mm. The emitter shroud's underside sits at
**9.296 mm** above the seat, so the plate passes under it with 0.9 mm to spare — and the two places
where the front arc meets the rear anchors sit exactly there. At 3 mm those necks measured **0.8 mm
across**, which is two extrusion widths. They are now **3.2 mm**, and the pocket grips the housing
over 6.4 mm of its height instead of 3.

**The emitter and the detector need different relief.** The emitter at 12.743 is above the plate, so
it simply looks out over the top; its cone (half-angle + 5°) is subtracted for margin. The detector
at **7.021 is below the 8.4 mm plate top**, so a cone would leave it peering down a conical tunnel
with a restricted field of view. It gets an **open trench** instead — everything above z = 4.0 in
front of it is removed, so it looks out over open sky.

A useful side effect: the trench floor blocks the detector's downward periphery (rays past about 20°
below axis) while leaving everything level and upward clear out to 35°. Downward is the floor-bounce
direction, so that is a baffle rather than an obstruction — and the emitter's beam does not descend
to z = 4 until roughly 50 mm out, well past the plate, so the floor is never illuminated.

The band now runs to radius 40.5 — 2 mm past the board rim. That is what buys the last of the neck
width, and it puts the plate slightly ahead of the housings, so it takes a knock before they do.

## Printing and fitting

- Plate-underside down; support under the two side feet only.
- Feet hook the board's **straight side edges at y = 58**, each with a 1.6 × 0.6 mm relief groove for
  the 5 V trace. Apex **M2 at (38.5, 98)**, nut under the board. Triangle 77 × 40 mm.
- Drop each housing through its pocket until it seats on the PCB, check azimuth, epoxy in the pocket.
- Fit the parts and form the leads as on the current mouse.

## Tight spots, honestly

- **Detector leads are the binding constraint.** SL uses 12.2 mm of a 12.70 mm minimum. Bend right at
  the housing's rear face and trim only after the lead is formed.
- Pads sit **0.4–2 mm behind** the rear face. Enough for the lead to drop, not enough to be casual about.
- Pocket clearance is 0.30 mm on both axes — change `FIT` in the source if your prints run tight.
- Free tilt inside the housing bores (±4–6° emitter, ±8° detector, Ch5) is unchanged. The plate fixes
  the housing's angle, not the part's angle within it.
- **Nothing has been printed.** Every figure is from the board file, the two datasheets and your STL.
