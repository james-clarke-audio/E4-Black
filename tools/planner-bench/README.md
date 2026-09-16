# planner-bench

Scores routes over the `.maz` files in `mazefiles/binary/` using the same
`planner.cpp` the firmware builds. No robot involved.

    make -f build.mk
    ./plan ../../mazefiles/binary/*.maz

Options:

| flag | meaning |
|---|---|
| `-v` | print the chosen QUICKEST route, move by move |
| `--vmax=N` | straight-line top speed, mm/s |
| `--accel=N` | forward acceleration, mm/s² (also sets decel) |
| `--no180` | forbid SS180, leaving stop-and-spin for a reversal |

## What it is for

Two questions that cannot be answered on a bench:

1. **Is a time-weighted plan worth anything at these numbers?** At the
   as-built config it is not, and the tool says so plainly: `SEARCH_SPEED` and
   `SEARCH_TURN_SPEED` are both 300 mm/s, so she goes exactly as fast down a
   straight as round a corner, every route of equal length takes equal time,
   and QUICKEST returns the same route as SHORTEST. The weighted planner only
   starts earning its keep once top speed on the straights is meaningfully
   above turn speed.

2. **How does the best route change as acceleration ramps between runs?**
   Rising `--accel` and `--vmax` together makes the planner start buying
   straights with distance -- on `japan2007ef` the chosen route lengthens from
   71 cells to 82 once speed is worth having, which is something a unit-cost
   flood can never do.

Both numbers are only as good as `turn_params`. The planner will confidently
pick a wrong route if the table lies to it, so the arcs need measuring, not
eyeballing.
