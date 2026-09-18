# Bench mazes: one rig per turn

Every turn she drives has to be profiled on the real floor, and profiling a
turn buried in the middle of a competition maze tells you very little -- by the
time she reaches it she is already carrying whatever error the turns before it
left behind. So each turn gets its own maze, small enough to build out of the
physical sections on the bench.

A section is 3 cells by 3 cells. Two of them butted together give 6x3. Each
maze here is a **single corridor**: no junctions anywhere, so there is exactly
one route through it and the planner cannot quietly decide to test something
else while you are watching the floor.

The generated files live in `mazefiles/bench/`, as both `.txt` (ASCII, carries
the goal cell) and `.maz` (binary, no goal). **Load the `.txt`** -- the goal is
the whole point and the binary form cannot express it.

## The start pose is part of the rig

On the bench she is placed by hand, so she does not have to begin in the
competition corner. That turns out to matter more than it sounds, because of
one piece of geometry:

> **(2,0) facing north is the mirror image of (0,0) facing north** -- and
> mirroring is exactly what swaps left for right.

Which is why the tables below come in pairs. Every left-hand rig is the
x-mirror of its right-hand twin, walk for walk, and the two plan to identical
times with every turn's hand flipped. `bench-3x3-90R` from (0,0) gives three
`ARC_R`; `bench-3x3-90L` from (2,0) gives three `ARC_L`. Same corridor shape,
same section, other hand.

From the competition corner alone none of that is available: facing north at
(0,0) with the west perimeter beside her, her first turn in a 3x3 can only be a
right, and `SD45_L` is not reachable at all without a spin in the route.

Her heading at the start is the first letter of the walk. The start cell always
has exactly one opening, so wherever she is put down her back is against a wall
to square up on. The ASCII files mark it `S`.

## The rigs

One section each -- four pairs, one pair per turn family:

| file | start | corridor | goal | route |
|---|---|---|---|---|
| `bench-3x3-90R`    | (0,0) N | `NNEESSW` | (1,0) | `ARC_R` x3, nothing else |
| `bench-3x3-90L`    | (2,0) N | `NNWWSSE` | (1,0) | `ARC_L` x3, nothing else |
| `bench-3x3-45minR` | (0,0) N | `NEN`     | (1,2) | `SD45_R DS45_L`, ONE diagonal step |
| `bench-3x3-45minL` | (2,0) N | `NWN`     | (1,2) | `SD45_L DS45_R`, ONE diagonal step |
| `bench-3x3-45R`    | (0,0) N | `NENE`    | (2,2) | `SD45_R DS45_R`, two steps |
| `bench-3x3-45L`    | (2,0) N | `NWNW`    | (0,2) | `SD45_L DS45_L`, two steps |
| `bench-3x3-dd90L`  | (0,0) N | `NENW`    | (0,2) | `SD45_R DD90_L DS45_L` |
| `bench-3x3-dd90R`  | (2,0) N | `NWNE`    | (2,2) | `SD45_L DD90_R DS45_R` |

Two sections each, for the runs that need room:

| file | start | corridor | goal | route |
|---|---|---|---|---|
| `bench-6x3-90R`    | (0,0) N | `NNEEEEESSWWWWN`   | (1,1) | four `ARC_R`, no other turn kind |
| `bench-6x3-90LR`   | (0,0) N | `NNEESSEEENNWWS`   | (3,1) | `ARC_R` x2 then `ARC_L` x4 |
| `bench-6x3-diag-R` | (0,0) N | `NENEEESESWWNWSW`  | (1,0) | `SD45_R` x3, `DS45_R` x4, both `DD90`s |
| `bench-6x3-diag-L` | (0,0) N | `NESEEENENWWSWNWW` | (0,2) | `SD45_L` x2, `DS45_L` x4, both `DD90`s |

## The 45 rigs are the offset test

`bench-3x3-45min{R,L}` is the smallest staircase that exists: `N, E, N`, one
diagonal step of 127.279 mm, with `sd45_offset` at one end and `ds45_offset` at
the other. Both offsets have to come out of that one step, so each must be
under 63.6 mm or the geometry does not close.

At the shipped 120/120 it does not close: the planner abandons the diagonal
entirely and drives `ARC_R`, `SPIN_L` instead. At 60/60 it drives `SD45_R`, one
step, `DS45_L`. Same maze, same mouse, and the only thing that changed was a
number -- which makes this the rig that tells you what the offsets really are.

`bench-3x3-45{R,L}` is the same idea one step longer: a two-step diagonal of
254.56 mm. It survives 120/120 with 14.6 mm of free diagonal left between the
turns, against 134.6 mm at 60/60. Run both and you can see where the boundary
actually sits rather than where the model says it should.

## Regenerating

    python3 mkbench.py ../../mazefiles/bench

`mkbench.py` holds the start poses and corridor walks and nothing else. The
walks came from `bench.cpp`, which enumerates every single-corridor maze that
fits in a section from every start pose it allows -- 644 of them in a 3x3 --
plans each with the real planner, and reports which rig shows each turn best:

    make -f build.mk bench
    ./bench --size=3x3 --off=60 --isolate
    ./bench --size=3x3 --off=60 --isolate --start=2,0,N

`--isolate` ranks a rig by how little else is in the route before how often the
turn appears; without it, repetitions win. `--start` pins the pose she will
actually be placed in, so what comes out is the rig to build rather than one of
its mirrors. Neither ranking will ever offer a rig containing a spin, because a
spin means she has stopped dead in the middle of the test and nothing is being
profiled at that moment.

## Checking

`checktxt` reads the shipped ASCII back -- walls and goal -- and **derives the
start pose from the file itself**: a single corridor has exactly two ends, one
of them is the goal, and the other is where she gets put down, heading along
its one opening. So the pose is verified rather than taken on trust, and what
gets checked is the file that goes to the mouse rather than the generator's
idea of it:

    make -f build.mk checktxt
    ./checktxt --off=60 ../../mazefiles/bench/*.txt

`check 0` on every line means the route is drivable. Run it with `--off=120`
too and watch the diagonal rigs fall back to arcs and spins: that is not a bug
in the mazes, it is the current offsets telling you they are too big.
