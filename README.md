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
- **A route executor** — one walk that either drives a planned route or
  rehearses it, so the rehearsal proves the sequencing the driver uses.
- **Time-weighted route planner** — least time rather than fewest cells, with
  diagonals *searched* rather than substituted, and an optimistic twin of each
  plan that says how much is still left to find.
- **Wall follower**, both hands, driven and simulated — its own competition
  class rather than a fallback solver.
- **A simulator that runs at the speed the motion model predicts**, so a
  simulated run takes as long as it claims to and the two numbers can be
  compared. Playback can be sped up for watching; the reported figure never
  moves with it.
- **A maze editor in the companion app** — draw a course on the big canvas,
  save it as text or `.maz`, and send it straight to her. Quicker than hunting
  through 406 files for one that exercises the thing you are testing.

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
("Staying Located"), a design note for the next board (STM32G474), how she
chooses her path, the turn table, and the sensor carrier.

## Repository layout

| Path | What it is |
|------|-----------|
| `Source/` | STM32CubeIDE firmware project (Core / Drivers / Modules / Program). |
| `CompanionApp/` | Native SwiftUI companion (macOS/iPad) + the `E4Core` package — **the tool she is driven from**. |
| `e4-maze.html` | Web Bluetooth companion app (single file); follows the Swift app rather than leading it. |
| `docs/` | The reference manual (`index.html` + chapters). |
| `3D Files/` | Sensor housings, wheels, mounts, and the sensor socket plate (STL, print notes, and the parametric source that generates and verifies it). |
| `tools/planner-bench/` | Native build of the route planner, for scoring routes against the maze files without a robot. |
| `tools/bench-mazes/` | Finds and writes the per-turn tuning rigs, and checks the shipped files by planning them. |
| `mazefiles/` | 408 real competition mazes, binary and text, plus `bench/` — twelve single-corridor tuning rigs. |
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

**0.33 — 18 Sep 2026 · The approach is part of the turn**
A tuning run is *drive to the turn, make it, drive out* — and the first of those
three was the one the tuner could not be asked for. `ARC`'s fifth field writes
`entry_offset`, and `run_arc_from_table()` passed `entry_offset` straight in as
the lead-in, so the arc always began that many millimetres after she started
moving. Fine for "does this arc look right"; useless for "does she come out of
it in the lane", which is the only question a bench answers.

The approach is now counted in cells and the millimetres derived:

| entered from | lead-in |
|---|---|
| a straight | `BACK_WALL_TO_CENTER + cells × FULL_CELL − entry_offset` |
| the diagonal | `cells × DIAG_PITCH − entry_offset` |

`DS45`, `DS135` and `DD90` begin **on** the diagonal, where the pitch is 127.279
and there is no back wall to square up on — she is placed on the line by hand.
Which case applies follows from the row rather than from a switch someone has to
remember: a turn that leaves the diagonal is a turn that was already on it.
`POS,<cells>` sets it, `TUNE,pos=,lead=,pitch=` reports what she will actually
drive *before* anything moves, and `TURNRES` carries the row and the lead so a
log line three runs back still means something.

**The card does the arithmetic before she does.** `R = v / ω`, and an arc joining
two lanes that cross at θ is tangent to both only at `R·tan(θ/2)` either side of
the crossing. There is one wrinkle, and it took drawing the maze out 1:1 to see
it: `entry_offset` is measured back from the **lattice point** the planner books
the turn at, which for most turns is also where the two lanes cross — but an
`SD45` is booked in at a **cell centre**, while a straight lane meets a diagonal
at the **wall midpoint 90 mm earlier**. That half cell is part of the offset.

| row | ω | R at 300 | tangent | + half cell | needs | has | |
|---|---|---|---|---|---|---|---|
| SS90E | 170 | 101 | 101 | — | 101 | 100 | agrees |
| DD90 | 273 | 63 | 63 | — | 63 | 63 | agrees |
| **DS45** | **95** | **181** | **75** | **—** | **75** | **120** | **does not** |
| **SD45** | **95** | **181** | **75** | **90** | **165** | **120** | **does not** |

So the two 45 rows cannot both be right while they share a number — one needs
165 and the other 75. The same 120 sits in the planner as `sd45_offset` /
`ds45_offset`, so those move with them. The Tuning screen shows the gap and
offers the figure; the floor decides which of the numbers was wrong, but it
cannot until they agree.

One number says it best: a diagonal step is 127.3 mm and `ds45_offset` is 120 of
it, so on `bench-3x3-45min` the DS45 gets **seven millimetres** of approach
before the arc starts. At the tangent value it gets 52. That is why the planner
abandons that geometry and drives `ARC_R, SPIN_L` instead.

*Alongside:* `E4-bench-diagnostics-A4.pdf` — the 1:1 cell template (centrelines,
corner-to-corner, and the diamond joining the four wall midpoints that is the
actual diagonal lane), plus a page per hand showing where the mouse gets put
down. `06fc12d` and `fa6d585` — `mazefiles/bench/`, twelve single-corridor
tuning rigs, and `tools/bench-mazes/` which found them by enumerating every
corridor that fits in a section and asking the real planner what each one
produces. Two results fell out of that search. A 3×3 entered at (0,0) facing
north can only turn right first, because the west perimeter is beside her —
`SD45_L` is unreachable there without a spin. And **(2,0) facing north is the
mirror image of (0,0) facing north**, so once the start pose is allowed to move,
every left-hand rig is the x-mirror of its right-hand twin, walk for walk.

**0.32 — 18 Sep 2026 · She can finish by turning into the goal**
Every goal test in `plan_native()` sat inside the cell-expansion loop, which
advances a cell *before* it looks. So a route that **arrives** at the goal by
turning into it was never recognised as finished: she drove past and came back,
or took a longer way round that happened to approach along a straight.

The node's own cell is now tested before the loop, with `best_cells = 0` and
`best_t = 0` because the turn that brought her there is already paid for in
`key`. `step_time()` returns `0.0f` for a zero-cell goal step rather than asking
`straight_time()` for a negative distance — the clock stops on *entering* the
goal, and what is left of that cell afterwards is not on it.

On Boston the last vertices went from `DS45_L (17,18) → (17,17) → GOAL (17,15)`
to `… → GOAL (17,17)`. Swept over the whole 394-maze corpus: **72 faster, 320
unchanged, 2 slower, 0 invalid**, mean 32.864 → 32.761 s. Best: japan1997f
−3.460 s, uk1992q −2.698 s, boston −1.544 s.

The two slower ones are not a regression in the fix. `key_of()` quantises time
to centiseconds — that is what makes Dial's bucket queue O(E) — so routes tying
on the quantised key can differ by up to ~10 ms a step, and ≤0.13 s over twenty
steps is inside one bucket width. Recorded here so nobody hunts it later.

**0.31 — 18 Sep 2026 · No toy maze at boot, and the editor can undo**
The boot scaffold seeded a two-cell maze with the goal at (1,1). It made a whole
class of mistake invisible: a Plan route run with **no maze sent** looked
entirely successful, because there really was a maze and she really did solve
it. She now boots with `maze.h`'s own `m_goal{7, 7}` and an open arena as ground
truth, so a run with nothing sent looks like what it is.

The companion app's maze editor grows undo — `history: [E4MazeFile]` with
`canUndo` / `undo()`, and ⌘Z on the canvas.

**0.30 — 17 Sep 2026 · She speeds up through ground she has already covered**
Watching a simulated explore, she never went any faster in cells she had already
been through — and she didn't, in either the simulator or the firmware.
`sim_move_seconds()` used `FULL_CELL / SEARCH_SPEED` for every cell, and
`search_to()` crawls at `SEARCH_SPEED` from the first cell to the last. The
simulator was faithfully showing what the firmware does, which is the simulator
doing its job.

**The test is per cell, not per leg.** The obvious version of this is to run the
whole return leg fast, on the grounds that she has just driven it. That is
wrong: the flood picks the best *known* path home and known does not mean
*visited* — she will have seen a cell's walls from next door without ever
entering it, and those cells are exactly where there is still something to
learn. So the return leg becomes an exploring run that happens to be quick in
the parts already covered.

`cruise_speed()` is that rule, in one place, used by the driven search and the
simulated one so neither models something the other does not do:

| condition | speed |
|---|---|
| the cell she is **entering** is fully visited, **and** the move out of it is straight | `RUN_SPEED` |
| anything else | `SEARCH_SPEED` |

The second condition is the one that is easy to forget. There is no room to shed
speed between a cell's sensing point and the turn point after it, so carrying run
speed into a turn means entering it too fast. Checking costs nothing: in visited
territory the map is complete, so `heading_to_smallest()` on the flood she
already has is a reliable one-cell look ahead.

**And a sloppy read at speed cannot hurt her**, which is what makes this safe
rather than merely fast. She still senses and still calls `update_map()` in a
cell crossed at 600 mm/s — but `update_wall_state()` refuses to change a wall
that has already been seen, and *fully visited* means every wall in that cell
has been. A bad reading is discarded by construction, in exactly the cells where
the rule allows speed. The only place a wrong wall could be written is a cell
with something unknown in it, and those are the cells she is required to crawl
through.

`sim_move_seconds()` now tracks the speed she is **carrying** and costs each move
with `timing::straight_time` — the planner's own model — so a run of visited
cells visibly winds up and the cell before a turn visibly sheds it, instead of
every cell costing a flat 0.6 s.

The wall follower stays at search speed throughout, deliberately: `cruise_speed()`
reads the flood to look ahead, and a follower never floods.

**0.29 — 17 Sep 2026 · The executor left a diagonal by the wrong side**
A diagonal speed run finished at **(1,0)** — near the start, nowhere near the
goal — having walked a staircase west and south with no 45° heading in it, on a
route whose time came back exactly right at 23199 ms.

One line. `run_route` left a diagonal with `nh = (L) ? ((dd + 3) & 3) : ((dd + 1) & 3)`.
"+3 to turn left" is correct *inside* an enum. This crosses from `Diag` to
`Head`, and those are different enums offset by half a turn: leaving a diagonal
to the left is **+0** and to the right is +1. The right hand was accidentally
correct, which is why it took a real route to expose.

What it does is worse than a wrong heading: it lands her on a **post**, the one
lattice point that is never occupied, so her half-cell coordinates lose their
parity and every orthogonal move afterwards keeps her on the wrong parity for
the rest of the route. Hence a whole run animated on the lines *between* cells —
which is exactly what the `POS` trail showed, every coordinate an even multiple
of 90.

Fixed in **one place, not two**: `diag_ccw`/`diag_cw` move from statics in
`native.cpp` to inline functions in `native.h`, so the executor uses the same
mapping `route_points()` does rather than a second copy of it. Two copies of that
mapping is how one of them went wrong.

And the walk checks itself now. A route ends at a cell **centre**, odd in both
half-cell coordinates; anything else means the walk lost the lattice and every
cell it has reported since is fiction. It says so — `SR,LOST u= v= diag=` —
rather than reporting a plausible wrong answer.

Verified on the maze that broke it: every vertex of the run now matches the
planned route exactly, and she finishes at (8,8).

**0.28 — 17 Sep 2026 · The route executor**
The planner has been able to answer since 0.20 and nothing acted on the answer.
`run_route()` does.

**One walk serves both the real run and the rehearsal.** The part worth getting
right is the sequencing — which turn, after how many cells, from which heading —
and a simulator that sequenced differently from the driver would prove nothing
about the driver. The only thing the `simulate` flag changes is whether a step
is animated or driven.

Timing comes from a new `route_times()` in `diagonal.cpp`, which walks the route
with the **same `step_time()` that costed it** and reports each step split into
the run before the turn and the turn itself. That split is what lets the
rehearsal animate a step at its true duration, and it means the simulated total
equals `route.seconds` by construction rather than by coincidence.

    Sim speed run   F    plan, then rehearse at the speed the model predicts
    Speed run       l    plan, then drive
    KIND,<0|1|2>         shortest · quickest · quickest with diagonals

Which route is a **setting** rather than three menu entries, because the three
are one run with a different cost function and the point is comparing them back
to back on one maze. The default is quickest orthogonal: the diagonal route is
faster on paper, but every diagonal row of the turn table is arithmetic that has
never met a floor.

Both actions **stream the route before executing it**, so the line she is about
to take is on screen before she takes it, and a run that goes wrong can be
compared against what she meant to do rather than against memory.

One simplification, named in the code rather than hidden: in the simulated turn,
position advances during the run and heading rotates at the vertex, so an arc
looks like a spin on screen. The *time* is right either way and the route line
shows the true path — but the animation is not evidence about arc geometry.
**Zigzag test** is what answers that.

> `act_speed_run` is **unproven**. No part of the driven path has been near a
> floor, and the turn table it reads is arithmetic outside the two SS90E rows.
> It arms and waits like everything else; give her room.

**0.27 — 17 Sep 2026 · The followers arm like everything else, and say so**
Two bugs, both from 0.25. `follow_to()` **did not wait** — every other action
that turns a wheel calls `sensors.wait_for_user_start()` before it moves, and
the driven wall follower went straight from a tap in the app to driving. It arms
now, and so does the simulated twin, so both halves of the pair behave the same
way and neither teaches a habit the other punishes.

The four followers were also missing from `waitsForButtonPress`, which is why
pressing one in the app looked like nothing happening: she printed
`armed: press a button to launch` and blocked, correctly, and the app had no way
to tell a deliberate pause from a dropped command. Both apps now put that
message in front of you rather than leaving it as one line among hundreds, and
the **Actions** screen gets the same armed banner the maze screen already had —
it had been showing `Running:` for an action doing nothing of the sort.

**0.26 — 17 Sep 2026 · The simulator runs at the speed the model predicts**
`sim_step` animated eight frames of thirty milliseconds whatever the move, so a
cell crossing, a 90 and a dead-end reversal all took 240 ms and the simulator
told you nothing about time. It takes a **duration** now and the frames follow
from it, and that duration comes from `timing.h` — the planner's own model, not
a second one written for the animation, so the two cannot drift into different
opinions about the same mouse.

| move | cost |
|---|---|
| `AHEAD` | one cell at search speed |
| 90 | an **arc**: she rotates *while* she travels, so it is whichever of the two takes longer, not the sum |
| `BACK` | the one move that does not overlap — `turn_back()` stops dead, spins on the spot and moves off again, so that one adds up |

Getting the 90 wrong the other way is how a simulator flatters a route full of
corners; getting `BACK` wrong is how it flatters one full of dead ends.

The reported time is now the **modelled** clock, not `HAL_GetTick()`. The two
agree to within the odd frame, which is exactly why the wall clock must not be
the one reported — a slow link or a stalled frame would read as a slower mouse,
and the figure would stop being a property of the route. Real search runs are
untouched; there the wall clock *is* the truth.

Two details decide whether it is honest. The frame deadline **absorbs** transmit
time rather than adding to it (a `POS` and `TEL` pair is about 56 bytes, roughly
10 ms of wire at 57600, so delaying a full frame on top would run every run a
third slow). And `SIM_RATE` — `SIM,<rate>` over BT, a picker in both apps —
scales the *watching* only, because a real-time explore is forty-odd seconds and
nobody should sit through that twice, but the number she reports must not move
when you skip ahead.

**0.25 — 17 Sep 2026 · The wall follower, both hands, driven and simulated**
`follow_to()` had been sitting in `mouse.h` implementing a left-hand follower the
whole time, with `act_wall_follow` still an `act_todo` stub, so nothing could
reach it. It takes a hand now:

    20  Wall follow L   w        32  Wall follow R   W
    33  Sim follow L    q        34  Sim follow R    Q

Four entries rather than one action with a hand setting, because the hand *is*
the experiment — left and right walk different halves of a course. The
simulated pair animate the same decision with the motors never armed, so both
hands can be run against any maze file with no bench at all.

**A wall follower is its own competition class, not a weaker solver.** A
follower course is built with a wall connected all the way to the centre, so a
follower always arrives — that is the point of the event. A maze-solver maze is
built the other way round, with the inside deliberately disconnected from the
outside, so a follower can never reach the middle of one however long it walks.
Same code, two events.

So `FOLLOW_STEP_LIMIT` (512, four times the cells in the arena) is not a guard
against a weak algorithm — on its own course it never trips. It exists because
this code gets pointed at a solver maze in the simulator, where **not arriving
is the correct answer**, and the only wrong behaviour would be walking for ever
while that is true. Giving up reports `WF,gave up after N cells (left hand)`.

Of the **406** files in `mazefiles/binary`, **90 are follower-solvable and 316
are not** — and it is the *same* 90 for both hands, with not one maze yielding
to one hand and not the other. That is structural rather than luck: the hand
decides which route she walks, not whether the centre is reachable. If the
centre's walls are connected to the perimeter, both hands are walking the same
wall component; if it is an island, neither can. Five of the 90 are built for
the class explicitly — `uk2010follower`, `uk2011follower-final`,
`tic05followersheats`, `robotic-2011-follower-heats`, `-finals` — and 33 are
competition mazes including `japan1991`, `uk2003q` and `uk2009f`. `test3.maz`
arrives in 14 cells if you only want a smoke test.

**0.23 — 16 Sep 2026 · How much of the route is still guesswork**
"Have I explored enough?" had no answer but a feeling. It has one now, and it is
a proof rather than a percentage.

`act_plan_route` already flips the wall mask, so the second plan is nearly free.
`MASK_CLOSED` treats an unseen wall as a wall and yields the best route she can
**prove**. `MASK_OPEN` treats it as an opening and yields the best route that
could **possibly** exist — a true lower bound, because the real maze has at
least as many walls as the optimistic view of it. The gap between them is the
most that is still out there to find, and when it closes her route is not
probably the best, it **is** the best and no further exploring can change it.

The cells worth driving to are exactly the not-yet-fully-seen cells *on* the
optimistic route. That is the sharp version of "explore what is left": an
unknown cell no optimistic route passes through cannot change the answer,
however blank it looks on the screen. A long corridor whose north walls she
learned from driving the row above, and whose east–west state she knows nothing
of, only matters if the best possible line wants to go down it.

    RB,<kind>,<ms>,<unknown cells on that route>
    RU,<x>,<y>  …  RUE,<total>        the union — and only these matter

The union is taken across all three kinds deliberately: it is a superset of what
an orthogonal-only run needs, which is the safe direction to be wrong in while
the diagonal arcs are untuned.

`route_cells()` in `native.cpp` walks a route and emits every **cell** it
covers, which is a different set from `route_points()`' corners — a six-cell
straight is two points and six cells, and a diagonal step is no point at all but
does cross one cell. The OLED shows known, best possible and the unseen count,
and reads `PROVED` when there is nothing left; both apps draw the unseen cells
as dashed amber squares under the route lines.

**0.22 — 16 Sep 2026 · Fast-run speeds, separate from the search speeds**
`act_plan_route` built its `Robot` from `SEARCH_SPEED` and `SEARCH_ACCELERATION`,
so it was planning the **fast run at search speed**. That is why `SHORTEST` and
`QUICKEST` came back at 27.245 s each with identical point lists on the bench
mouse: told that a straight is no quicker than a corner, the planner has nothing
to choose between.

Search speed is bounded by sensing — she reads walls and decides where to go
once per cell — and is settled at the bench. The fast run has no such limit,
because the map is already known.

    RUN_SPEED         600 mm/s     twice the search speed, a timid opening bid
    RUN_ACCELERATION  2000 mm/s²   as the search, until it is measured
    RUN_DIAG_SPEED    300 mm/s     the diagonal corridor is 110.3 mm between
                                   posts against 168 down a cell — about 14 mm
                                   a side with the plate on. She threads it.

The **arc** speeds are untouched and stay as the turn table holds them. They are
independent: she can run a six-cell straight flat out and still take every SS90
at the speed its row was tuned at, and that independence is the whole mechanism
by which a longer, straighter line starts to beat a shorter, twistier one.

Runtime rather than `const`, like the turn table and the spin dynamics — `SPD?`
and `SPD,v,a,diag` over BT, persisted as config block **v6**. Nothing drives on
them yet (`act_speed_run` is still a stub), so a wrong value costs an estimate,
not a mouse.

Sweeping the straight speed across the full competition mazes says where her lap
time actually lives:

| `v_max` | 300 | 600 | 1000 | 1500 |
|---|---|---|---|---|
| tricky.maz | 67.83 s | 51.93 s | 47.87 s | 46.80 s |
| APEC2017.maz | 71.97 s | 52.39 s | 46.93 s | 45.02 s |

300 → 600 buys 23%. 600 → 1500 — two and a half times the straight speed — buys
another 10%. The routes are **turn-dominated**: sixteen arcs at 300 mm/s swamp
what the straights can save, so the turn table is worth more bench time than the
straight-line number.

Two fixes landed with it. `act_plan_route` was planning to a **1×1 goal** when
the goal is a 2×2 room, so she drove *through* two goal cells to reach the one
square she had been told to reach — work done after the clock had already
stopped. And the app was never sent the **perimeter**: `maze.initialise()` walls
the outside of the arena before a single sensor reading, but `W` lines are only
sent for cells she stands in, so the border was known at both ends of the link
and sent over neither.

**0.21 — 16 Sep 2026 · The planner reaches the app**
`Plan route` (Maze solver, or `P`) runs the planner three ways over the map she
is currently holding — shortest, quickest, quickest-with-diagonals — and streams
each to the companion apps, which draw all three over the same maze. No motors,
so it works on an injected maze, a remembered one, or one she has just explored:
a whole run can be rehearsed at the bench before she drives a cell of it.

Points go out in **half-cells**, which is the unit the planner's lattice already
works in. Cell centres land on odd coordinates and wall midpoints on mixed ones,
so a diagonal arrives at the app as an ordinary pair of points — neither
companion has to know about the lattice, the parity rule, or which wall a point
sits on. Draw a polyline through them and it is right.

    RT,<kind>,<ms>                      kind 0 shortest, 1 quickest, 2 diagonal
    RP,<u>,<v>,<move>                   a vertex in half-cells
    RTE,<points>,<cells>,<turns>,<spins>

The firmware carries only `native.cpp`: with diagonals disabled it *is* the
orthogonal planner, and `tools/planner-bench/xcheck` confirms the two agree in
shape on all 392 mazes. That keeps `planner.cpp`'s 36 KB out of the build.

Both apps also gain **Zigzag test** and the `ZIG,turns,mode,first` setup, so the
chained-arc question can be settled without the OLED.

Documented in [Ch 12 — Planning the Fast Run](docs/e4-planner.html); the
[Build Map](docs/e4-build-map.html) now shows the diagonal capstone as half
done — the planner is shipped, the 45° *motion* is what is left.

**0.20 — 16 Sep 2026 · Diagonals, searched rather than substituted**
`native.cpp` puts the diagonal moves into the graph and searches, instead of
planning orthogonally and rewriting afterwards. Work in half-cells and parity
says what a point is — (odd,odd) a cell centre, mixed a wall midpoint,
(even,even) a post — and a diagonal is a straight line through wall midpoints
stepping ±1,±1, so it never lands on a post. It is *not* the line joining cell
centres diagonally: that goes straight through one. Because it searches, it can
take a **longer** cell route because that route diagonalises better, which is
the one thing post-processing can never do.

Across 392 real competition mazes, against the orthogonal quickest route:
classic substitution **−3.4%**, native search **−13.2%**, best −47.6% on
`quo4`.

`route_check()` walks a route over the lattice against the map, independently
of whichever planner produced it — because the two disagreed and a planner that
scores its own output is only as honest as its own model. It found that classic
was reporting times for routes she could not drive: the exit hand off a
diagonal is fixed by the lattice rather than by the zigzag, and the diagonal
does not end in the cell the zigzag ended in. Fixing both took classic from a
claimed 8–10% to a real 3.4%. Both methods now pass on all 392.

Documented in [Ch 12 — Planning the Fast Run](docs/e4-planner.html). Still
nothing calls any of it, so there is still no speed run.

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
