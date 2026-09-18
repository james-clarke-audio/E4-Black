#!/usr/bin/env python3
"""Write the per-turn bench mazes.

Each one is a single corridor inside a block of cells at the bottom-left of a
standard 16x16 file -- a 3x3 block is one of James's physical sections, a 6x3
block is two of them butted together. Everything outside the block is sealed,
so she can never leave the section no matter what goes wrong.

The corridor is given as a walk from the start cell: a string of NESW. Those
walks came out of bench.cpp, which enumerated every single-corridor maze that
fits in the section and asked the real planner which turns each one produces.

Walls are a bitmask per cell: bit0 N, bit1 E, bit2 S, bit3 W. A set bit is a
wall, which is what the firmware's WallReader expects.
"""
import os
import sys

N, E, S, W = 1, 2, 4, 8
LOOK = {"N": N, "E": E, "S": S, "W": W}
DX = {N: 0, E: 1, S: 0, W: -1}
DY = {N: 1, E: 0, S: -1, W: 0}
OPP = {N: S, E: W, S: N, W: E}
SIZE = 16


class Maze:
    def __init__(self):
        self.w = [[N | E | S | W for _ in range(SIZE)] for _ in range(SIZE)]
        self.goals = []

    def open(self, x, y, d):
        self.w[x][y] &= ~d
        nx, ny = x + DX[d], y + DY[d]
        if 0 <= nx < SIZE and 0 <= ny < SIZE:
            self.w[nx][ny] &= ~OPP[d]

    def corridor(self, walk):
        x = y = 0
        for ch in walk:
            d = LOOK[ch]
            self.open(x, y, d)
            x, y = x + DX[d], y + DY[d]
        return x, y

    def maz(self):
        b = bytearray(SIZE * SIZE)
        for x in range(SIZE):
            for y in range(SIZE):
                b[x * SIZE + y] = self.w[x][y]
        return bytes(b)

    def txt(self):
        """The ukmars ASCII form: 33 lines of 65 columns, y=15 on the top row."""
        out = []
        for y in range(SIZE - 1, -1, -1):
            top = "".join("o" + ("---" if self.w[x][y] & N else "   ") for x in range(SIZE))
            mid = "".join(("|" if self.w[x][y] & W else " ")
                          + (" G " if (x, y) in self.goals else "   ") for x in range(SIZE))
            out.append(top + "o")
            out.append(mid + ("|" if self.w[SIZE - 1][y] & E else " "))
        out.append("".join("o" + ("---" if self.w[x][0] & S else "   ") for x in range(SIZE)) + "o")
        return "\n".join(out) + "\n"


# name, section, corridor walk from (0,0), goal cell, what it is for
RIGS = [
    ("bench-3x3-90",      "3x3", "NNEESSW",          (1, 0),
     "three right 90s, and nothing else in the route"),
    ("bench-3x3-90L",     "3x3", "NEEN",             (2, 2),
     "one right 90 then one left -- the only left a 3x3 can reach from the start"),
    ("bench-3x3-45",      "3x3", "NENE",             (2, 2),
     "the smallest staircase: SD45 in, one 127.3 mm diagonal step, DS45 out"),
    ("bench-3x3-45min",   "3x3", "NEN",              (1, 2),
     "the one-step diagonal: 127.3 mm has to swallow BOTH offsets, so this is the"
     " tightest piece of geometry she will ever be asked to drive"),
    ("bench-3x3-dd90",    "3x3", "NENW",             (0, 2),
     "the diagonal changes hand at a wall midpoint: SD45_R, DD90_L, DS45_L"),
    ("bench-6x3-90R",     "6x3", "NNEEEEESSWWWWN",   (1, 1),
     "four right 90s with real straights between them, no other turn kind"),
    ("bench-6x3-90LR",    "6x3", "NNEESSEEENNWWS",   (3, 1),
     "two rights then four lefts -- both hands in one run"),
    ("bench-6x3-diag-R",  "6x3", "NENEEESESWWNWSW",  (1, 0),
     "every right-hand diagonal move: SD45_R, DS45_R, DD90_R (and one DD90_L)"),
    ("bench-6x3-diag-L",  "6x3", "NESEEENENWWSWNWW", (0, 2),
     "every left-hand diagonal move: SD45_L, DS45_L, DD90_L (and one DD90_R)"),
]


def build(rig):
    _, _, walk, goal, _ = rig
    m = Maze()
    end = m.corridor(walk)
    if end != goal:
        raise SystemExit("%s: walk ends at %s but goal is %s" % (rig[0], end, goal))
    m.goals.append(goal)
    return m


if __name__ == "__main__":
    d = sys.argv[1] if len(sys.argv) > 1 else "bench"
    os.makedirs(d, exist_ok=True)
    for rig in RIGS:
        name, sec, walk, goal, note = rig
        m = build(rig)
        open(os.path.join(d, name + ".txt"), "w").write(m.txt())
        open(os.path.join(d, name + ".maz"), "wb").write(m.maz())
        print("%-18s %-4s %-17s goal %-7s %s" % (name, sec, walk, str(goal), note))
