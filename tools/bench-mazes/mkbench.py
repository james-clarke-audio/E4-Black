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
        self.start = None

    def open(self, x, y, d):
        self.w[x][y] &= ~d
        nx, ny = x + DX[d], y + DY[d]
        if 0 <= nx < SIZE and 0 <= ny < SIZE:
            self.w[nx][ny] &= ~OPP[d]

    def corridor(self, walk, start=(0, 0)):
        x, y = start
        self.start = start
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
                          + (" G " if (x, y) in self.goals
                             else " S " if (x, y) == self.start else "   ")
                          for x in range(SIZE))
            out.append(top + "o")
            out.append(mid + ("|" if self.w[SIZE - 1][y] & E else " "))
        out.append("".join("o" + ("---" if self.w[x][0] & S else "   ") for x in range(SIZE)) + "o")
        return "\n".join(out) + "\n"


# name, section, start cell, corridor walk from the start, goal cell, what it is for
#
# The start pose is part of the rig. On the bench she is placed by hand, so she
# does not have to begin in the competition corner -- and that matters, because
# (2,0) facing north is the MIRROR IMAGE of (0,0) facing north, and mirroring is
# exactly what swaps left for right. Every left-hand rig below is the x-mirror
# of its right-hand twin, walk for walk. Her heading at the start is always the
# first letter of the walk; the start cell has exactly one opening, so her back
# is against a wall wherever she is put down.
RIGS = [
    ("bench-3x3-90R",     "3x3", (0, 0), "NNEESSW",          (1, 0),
     "three right 90s, and nothing else in the route"),
    ("bench-3x3-90L",     "3x3", (2, 0), "NNWWSSE",          (1, 0),
     "three left 90s -- the mirror of 90R, and the reason the start pose matters"),
    ("bench-3x3-45minR",  "3x3", (0, 0), "NEN",              (1, 2),
     "the one-step diagonal, right hand in: 127.3 mm has to swallow BOTH offsets"),
    ("bench-3x3-45minL",  "3x3", (2, 0), "NWN",              (1, 2),
     "the one-step diagonal, left hand in"),
    ("bench-3x3-45R",     "3x3", (0, 0), "NENE",             (2, 2),
     "two diagonal steps, right hand both ends"),
    ("bench-3x3-45L",     "3x3", (2, 0), "NWNW",             (0, 2),
     "two diagonal steps, left hand both ends"),
    ("bench-3x3-dd90L",   "3x3", (0, 0), "NENW",             (0, 2),
     "the diagonal changes hand at a wall midpoint: SD45_R, DD90_L, DS45_L"),
    ("bench-3x3-dd90R",   "3x3", (2, 0), "NWNE",             (2, 2),
     "the same at the other hand: SD45_L, DD90_R, DS45_R"),
    ("bench-6x3-90R",     "6x3", (0, 0), "NNEEEEESSWWWWN",   (1, 1),
     "four right 90s with real straights between them, no other turn kind"),
    ("bench-6x3-90LR",    "6x3", (0, 0), "NNEESSEEENNWWS",   (3, 1),
     "two rights then four lefts -- both hands in one run"),
    ("bench-6x3-diag-R",  "6x3", (0, 0), "NENEEESESWWNWSW",  (1, 0),
     "every right-hand diagonal move: SD45_R x3, DS45_R x4, DD90_R, DD90_L"),
    ("bench-6x3-diag-L",  "6x3", (0, 0), "NESEEENENWWSWNWW", (0, 2),
     "every left-hand diagonal move: SD45_L x2, DS45_L x4, DD90_L, DD90_R"),
]


def build(rig):
    _, _, start, walk, goal, _ = rig
    m = Maze()
    end = m.corridor(walk, start)
    if end != goal:
        raise SystemExit("%s: walk ends at %s but goal is %s" % (rig[0], end, goal))
    m.goals.append(goal)
    return m


if __name__ == "__main__":
    d = sys.argv[1] if len(sys.argv) > 1 else "bench"
    os.makedirs(d, exist_ok=True)
    for rig in RIGS:
        name, sec, start, walk, goal, note = rig
        m = build(rig)
        open(os.path.join(d, name + ".txt"), "w").write(m.txt())
        open(os.path.join(d, name + ".maz"), "wb").write(m.maz())
        print("%-18s %-4s start %-6s facing %s  %-17s goal %-7s %s"
              % (name, sec, str(start), walk[0], walk, str(goal), note))
