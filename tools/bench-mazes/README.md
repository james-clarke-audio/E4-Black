# Bench mazes: one rig per turn

Every turn she drives has to be profiled on the real floor, and profiling a
turn buried in the middle of a competition maze tells you very little -- by the
time she reaches it she has already accumulated whatever error the turns before
it left behind. So each turn gets its own maze, small enough to build out of
the physical sections on the bench.

A section is 3 cells by 3 cells. Two of them butted together give 6x3. The
mazes here come in both sizes, and each one is a **single corridor**: no
junctions anywhere, so there is exactly one route through it and the planner
cannot quietly decide to test something else.

The generated files live in `mazefiles/bench/`, as both `.txt` (ASCII, carries
the goal cell) and `.maz` (binary, no goal). **Load the `.txt`** -- the goal is
the whole point, and the binary form cannot express it.

## The rigs

One section each:

| file | corridor from (0,0) | goal | route |
|---|---|---|---|
| `bench-3x3-90`    | `NNEESSW` | (1,0) | `ARC_R ARC_R ARC_R` |
| `bench-3x3-90L`   | `NEEN`    | (2,2) | `ARC_R ARC_L` |
| `bench-3x3-45`    | `NENE`    | (2,2) | `SD45_R DS45_R`, two diagonal steps |
| `bench-3x3-45min` | `NEN`     | (1,2) | `SD45_R DS45_L`, ONE diagonal step |
| `bench-3x3-dd90`  | `NENW`    | (0,2) | `SD45_R DD90_L DS45_L` |

Two sections each:

| file | corridor from (0,0) | goal | route |
|---|---|---|---|
| `bench-6x3-90R`     | `NNEEEEESSWWWWN`   | (1,1) | four `ARC_R`, nothing else |
| `bench-6x3-90LR`    | `NNEESSEEENNWWS`   | (3,1) | `ARC_R` x2 then `ARC_L` x4 |
| `bench-6x3-diag-R`  | `NENEEESESWWNWSW`  | (1,0) | `SD45_R` x3, `DS45_R` x4, `DD90_R`, `DD90_L` |
| `bench-6x3-diag-L`  | `NESEEENENWWSWNWW` | (0,2) | `SD45_L` x2, `DS45_L` x4, `DD90_L`, `DD90_R` (plus one right-hand entry to get her into position) |

Two of those want explaining.

`bench-3x3-90L` looks feeble next to its right-hand twin, and it is. She starts
at (0,0) facing north with the west perimeter beside her, so her first turn in
a 3x3 can only be a right. Every left-hand rig therefore has to spend a turn
getting into position first, and a 3x3 has no room left over afterwards. If the
left hand matters -- and it does, because a mechanical asymmetry is exactly the
thing a left/right pair catches -- use `bench-6x3-90LR`, which gets four of
them.

The two 45 rigs are the offset test, and they are a pair on purpose.

`bench-3x3-45min` is the smallest staircase that exists: `N, E, N`, one
diagonal step of 127.279 mm, with `sd45_offset` at one end and `ds45_offset` at
the other. Both offsets have to come out of that one step, so each must be
under 63.6 mm or the geometry does not close. At the shipped 120/120 it does
not: the planner gives up on the diagonal entirely and drives `ARC_R`, `SPIN_L`
instead. At 60/60 it drives `SD45_R`, one step, `DS45_L`. Same maze, same
mouse, and the only thing that changed was a number -- which makes this the
rig that tells you what the offsets really are.

`bench-3x3-45` is the same idea one step longer: `N, E, N, E`, a two-step
diagonal of 254.56 mm. It survives 120/120 with 14.6 mm of free diagonal left
between the turns, against 134.6 mm at 60/60. Run both and you can see where
the boundary actually sits rather than where the model says it should.

## Regenerating

    python3 mkbench.py ../../mazefiles/bench

`mkbench.py` holds the corridor walks and nothing else -- the walks themselves
came from `bench.cpp`, which enumerates every single-corridor maze that fits in
a section (78 of them in a 3x3, 4036 in a 6x3), plans each one with the real
planner, and reports which layout shows each turn best:

    make -f build.mk bench
    ./bench --size=6x3 --off=60 --isolate

`--isolate` ranks a rig by how little else is in the route before how often the
turn appears; without it, repetitions win. Neither ranking will offer a rig
containing a spin, because a spin means she stops dead in the middle of the
test and nothing is being profiled at that moment.

## Checking

`checktxt` reads the shipped ASCII back -- walls, goal and all -- and plans it,
so the thing being verified is the file that goes to the mouse rather than the
generator's idea of it:

    make -f build.mk checktxt
    ./checktxt --off=60 ../../mazefiles/bench/*.txt

`check 0` on every line means the route is drivable. Run it with `--off=120`
too and watch the diagonal rigs fall back to arcs and spins: that is not a bug
in the mazes, it is the current offsets telling you they are too big.
