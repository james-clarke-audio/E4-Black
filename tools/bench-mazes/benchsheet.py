#!/usr/bin/env python3
"""The 1:1 cell template, and the pair of chained-rig overview pages.

page_cell() is the only 1:1 sheet there is and turnpages.py imports it as
page 1 of the bench document -- edit it here, not there.

The two rig pages this file can still draw show BOTH 45s in one run. That
is a check, not a measurement: a lateral error out of the first turn moves
the start of the second, so the finish only ever tells you their sum. They
are kept because the picture of the whole rig is useful, but the pages that
go in the document are the single-turn ones in turnpages.py.

The geometry is not eyeballed. Cell centres live at half-cell lattice
coordinates (odd, odd); wall midpoints at (odd, even) and (even, odd); posts
at (even, even). A diagonal step moves (+/-1, +/-1), so it always goes wall
midpoint to wall midpoint and never touches a post or a cell centre. Written
out in millimetres the diagonal running lines are

    x + y = 90 + 180n        and        x - y = 90 + 180n

which inside any one cell is the DIAMOND joining the four wall midpoints.
Each edge is 90*sqrt(2) = 127.279 mm, which is DIAG_PITCH exactly.
"""
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.lib import colors
from reportlab.pdfgen import canvas
import math

CELL = 180.0
HALF = 90.0
POST = 12.0
DIAG = 90.0 * math.sqrt(2.0)          # 127.279
BACK_WALL_TO_CENTER = 49.0

INK     = colors.HexColor("#111111")
CONSTR  = colors.HexColor("#9aa0a6")   # cell boundary, posts outline
FAINT   = colors.HexColor("#c9ced4")   # corner-to-corner X
CENTRE  = colors.HexColor("#6b7280")   # orthogonal centrelines
RACE    = colors.HexColor("#1558d6")   # the diagonal running line
PATH    = colors.HexColor("#d1332e")   # what she actually drives
MOUSE   = colors.HexColor("#2f6f3e")
PAPER   = colors.HexColor("#ffffff")


# --------------------------------------------------------------------------
def dash(c, pattern):
    c.setDash(pattern)


def line(c, x1, y1, x2, y2):
    c.line(x1 * mm, y1 * mm, x2 * mm, y2 * mm)


def text(c, x, y, s, size=8, col=INK, font="Helvetica", anchor="l"):
    c.setFillColor(col)
    c.setFont(font, size)
    if anchor == "c":
        c.drawCentredString(x * mm, y * mm, s)
    elif anchor == "r":
        c.drawRightString(x * mm, y * mm, s)
    else:
        c.drawString(x * mm, y * mm, s)


def rot_text(c, x, y, ang, s, size=7, col=INK, font="Helvetica"):
    c.saveState()
    c.translate(x * mm, y * mm)
    c.rotate(ang)
    c.setFillColor(col)
    c.setFont(font, size)
    c.drawCentredString(0, 0, s)
    c.restoreState()


# --------------------------------------------------------------------------
def page_cell(c):
    """1:1 -- one cell, every diagnostic line, ready to mark through."""
    ox, oy = 15.0, 70.0                      # SW post centre on the page
    def X(u): return ox + u
    def Y(v): return oy + v

    text(c, 105, 285, "E4 \u2014 cell template, 1:1", 15, INK, "Helvetica-Bold", "c")
    text(c, 105, 278.5, "lay it in a cell, square it to the posts, mark through the four midpoints",
         9, CENTRE, "Helvetica", "c")

    # --- where the walls sit, so the lane distances mean something --------
    c.setFillColor(colors.HexColor("#eceff1"))
    for (bx, by, w, h) in ((0, -POST / 2, CELL, POST), (0, CELL - POST / 2, CELL, POST),
                           (-POST / 2, 0, POST, CELL), (CELL - POST / 2, 0, POST, CELL)):
        c.rect(X(bx) * mm, Y(by) * mm, w * mm, h * mm, stroke=0, fill=1)

    # --- cell boundary and posts ------------------------------------------
    c.setStrokeColor(CONSTR); c.setLineWidth(0.5)
    c.rect(X(0) * mm, Y(0) * mm, CELL * mm, CELL * mm, stroke=1, fill=0)
    c.setFillColor(CONSTR)
    for px in (0, CELL):
        for py in (0, CELL):
            c.rect((X(px) - POST / 2) * mm, (Y(py) - POST / 2) * mm,
                   POST * mm, POST * mm, stroke=0, fill=1)
            # registration cross on the post centre
            c.setStrokeColor(INK); c.setLineWidth(0.6)
            line(c, X(px) - 9, Y(py), X(px) - 4, Y(py))
            line(c, X(px) + 4, Y(py), X(px) + 9, Y(py))
            line(c, X(px), Y(py) - 9, X(px), Y(py) - 4)
            line(c, X(px), Y(py) + 4, X(px), Y(py) + 9)
            c.setStrokeColor(CONSTR); c.setLineWidth(0.5)

    # --- corner to corner, the angle check --------------------------------
    c.setStrokeColor(FAINT); c.setLineWidth(0.6)
    line(c, X(0), Y(0), X(CELL), Y(CELL))
    line(c, X(CELL), Y(0), X(0), Y(CELL))

    # --- orthogonal centrelines -------------------------------------------
    c.setStrokeColor(CENTRE); c.setLineWidth(0.8); dash(c, [4, 3])
    line(c, X(HALF), Y(0), X(HALF), Y(CELL))
    line(c, X(0), Y(HALF), X(CELL), Y(HALF))
    dash(c, [])

    # --- the diamond: the diagonal running line ---------------------------
    mids = [(HALF, 0), (CELL, HALF), (HALF, CELL), (0, HALF)]
    c.setStrokeColor(RACE); c.setLineWidth(1.5)
    for i in range(4):
        a, b = mids[i], mids[(i + 1) % 4]
        line(c, X(a[0]), Y(a[1]), X(b[0]), Y(b[1]))
    c.setFillColor(RACE)
    for (mx_, my_) in mids:
        c.circle(X(mx_) * mm, Y(my_) * mm, 1.7 * mm, stroke=0, fill=1)

    # centre point
    c.setStrokeColor(CENTRE); c.setLineWidth(1.0)
    line(c, X(HALF) - 5, Y(HALF), X(HALF) + 5, Y(HALF))
    line(c, X(HALF), Y(HALF) - 5, X(HALF), Y(HALF) + 5)

    # --- dimensions, all inside the square --------------------------------
    rot_text(c, X(135) - 6, Y(45) - 6, 45, "127.279 mm = DIAG_PITCH", 8, RACE)
    rot_text(c, X(45) - 6, Y(135) + 6, 45, "one diagonal step", 7.5, RACE)
    text(c, X(HALF) + 7, Y(HALF) + 2, "cell centre", 7.5, CENTRE)
    text(c, X(14), Y(HALF) + 5, "wall midpoints \u2014 the four points it all hangs on", 7.5, RACE)

    # the 90s, as arrowless dimension ticks along the south edge
    c.setStrokeColor(CONSTR); c.setLineWidth(0.5)
    line(c, X(0), Y(-16), X(HALF), Y(-16))
    line(c, X(HALF), Y(-16), X(CELL), Y(-16))
    for x in (0, HALF, CELL):
        line(c, X(x), Y(-18.5), X(x), Y(-13.5))
    text(c, X(45), Y(-14.5), "90", 7.5, CONSTR, "Helvetica", "c")
    text(c, X(135), Y(-14.5), "90", 7.5, CONSTR, "Helvetica", "c")
    text(c, X(CELL) - 2, Y(-22), "180 post to post", 7, CONSTR, "Helvetica", "r")

    # --- legend ------------------------------------------------------------
    def key(y, col, w, dsh, label, note):
        c.setStrokeColor(col); c.setLineWidth(w); dash(c, dsh)
        line(c, 16, y + 1.2, 31, y + 1.2)
        dash(c, [])
        text(c, 34, y, label, 8.5, INK, "Helvetica-Bold")
        text(c, 34, y - 4.3, note, 7.6, CENTRE)

    key(44, RACE,   1.5, [],     "diagonal running line",
        "joins the four wall midpoints. The lane she is on between an SD45 and a DS45.")
    key(33, CENTRE, 0.8, [4, 3], "orthogonal centrelines",
        "the straight lanes \u2014 90 mm from the boundary, 84 mm clear of a wall face.")
    key(22, FAINT,  0.6, [],     "corner to corner",
        "post to post. For checking squareness and 45 by eye \u2014 she never drives on it.")

    text(c, 16, 13,
         "To mark it out: find each wall midpoint (90 mm from either post) and join the four. That diamond passes a post on every",
         7.6, INK)
    text(c, 16, 9,
         "side without touching one, which is the whole point of it. The cell centre is NOT on it \u2014 that is what the 45s have to bridge.",
         7.6, CENTRE)

    # --- print-scale check -------------------------------------------------
    c.setStrokeColor(INK); c.setLineWidth(0.8); dash(c, [])
    line(c, 55, 264, 155, 264)
    for x in (55, 155):
        line(c, x, 261.5, x, 266.5)
    text(c, 105, 256.5, "100.0 mm \u2014 measure this first. If it is not 100, the print was scaled.",
         8, INK, "Helvetica-Bold", "c")

# --------------------------------------------------------------------------
NORTH, EAST, SOUTH, WEST = 0, 1, 2, 3
DX = {NORTH: 0, EAST: 1, SOUTH: 0, WEST: -1}
DY = {NORTH: 1, EAST: 0, SOUTH: -1, WEST: 0}
LOOK = {"N": NORTH, "E": EAST, "S": SOUTH, "W": WEST}


def corridor_walls(start, walk, n=3):
    """Every wall in an n x n section, minus the ones the corridor opens."""
    walls = {}
    for x in range(n):
        for y in range(n):
            walls[(x, y)] = {NORTH, EAST, SOUTH, WEST}
    x, y = start
    cells = [(x, y)]
    for ch in walk:
        d = LOOK[ch]
        walls[(x, y)].discard(d)
        x, y = x + DX[d], y + DY[d]
        walls[(x, y)].discard((d + 2) % 4)
        cells.append((x, y))
    return walls, cells


def page_rig(c, title, start, walk, hand, notes, scale=1 / 3.2):
    """A 45 tuning rig, with the mouse where she gets put down."""
    n = 3
    span = n * CELL * scale                     # 180 mm at 1:3
    ox = (210 - span) / 2.0
    oy = 100.0

    def X(u): return ox + u * scale
    def Y(v): return oy + v * scale

    text(c, 105, 285, title, 14, INK, "Helvetica-Bold", "c")
    text(c, 105, 278.5, "one 3\u00d73 section \u2014 a picture of where she goes. Not to scale: page 1 is the one you measure from.",
         8.5, CENTRE, "Helvetica", "c")

    walls, cells = corridor_walls(start, walk, n)

    # --- the diagnostic lines, every cell ---------------------------------
    for cx in range(n):
        for cy in range(n):
            bx, by = cx * CELL, cy * CELL
            c.setStrokeColor(FAINT); c.setLineWidth(0.35); dash(c, [])
            line(c, X(bx), Y(by), X(bx + CELL), Y(by + CELL))
            line(c, X(bx + CELL), Y(by), X(bx), Y(by + CELL))
            c.setStrokeColor(CENTRE); c.setLineWidth(0.35); dash(c, [2, 2])
            line(c, X(bx + HALF), Y(by), X(bx + HALF), Y(by + CELL))
            line(c, X(bx), Y(by + HALF), X(bx + CELL), Y(by + HALF))
            dash(c, [])
            c.setStrokeColor(RACE); c.setLineWidth(0.55)
            m = [(bx + HALF, by), (bx + CELL, by + HALF),
                 (bx + HALF, by + CELL), (bx, by + HALF)]
            for i in range(4):
                a, b = m[i], m[(i + 1) % 4]
                line(c, X(a[0]), Y(a[1]), X(b[0]), Y(b[1]))

    # --- walls -------------------------------------------------------------
    c.setStrokeColor(INK); c.setLineWidth(2.4); dash(c, [])
    for (cx, cy), w in walls.items():
        bx, by = cx * CELL, cy * CELL
        if NORTH in w: line(c, X(bx), Y(by + CELL), X(bx + CELL), Y(by + CELL))
        if SOUTH in w: line(c, X(bx), Y(by), X(bx + CELL), Y(by))
        if EAST  in w: line(c, X(bx + CELL), Y(by), X(bx + CELL), Y(by + CELL))
        if WEST  in w: line(c, X(bx), Y(by), X(bx), Y(by + CELL))
    c.setFillColor(INK)
    for i in range(n + 1):
        for j in range(n + 1):
            px, py = i * CELL, j * CELL
            c.rect((X(px) - 2.4) * mm, (Y(py) - 2.4) * mm, 4.8 * mm, 4.8 * mm,
                   stroke=0, fill=1)

    # --- the path she drives ----------------------------------------------
    for seg in notes["path"]:
        c.setStrokeColor(PATH); c.setLineWidth(2.0); dash(c, [])
        line(c, X(seg[1]), Y(seg[2]), X(seg[3]), Y(seg[4]))

    # --- the mouse, where she gets put down -------------------------------
    sx, sy = start
    mx = sx * CELL + HALF
    axle = sy * CELL + HALF - BACK_WALL_TO_CENTER
    bw, bl = 76.0, 92.0                       # body, indicative only
    c.setStrokeColor(MOUSE); c.setLineWidth(1.1)
    c.setFillColor(colors.HexColor("#dfeade"))
    c.roundRect(X(mx - bw / 2) * mm, Y(sy * CELL + 5) * mm,
                bw * scale * mm, bl * scale * mm, 2.2 * mm, stroke=1, fill=1)
    c.setFillColor(MOUSE)
    p = c.beginPath()                          # nose
    p.moveTo(X(mx) * mm, Y(sy * CELL + 5 + bl + 10) * mm)
    p.lineTo(X(mx - 8) * mm, Y(sy * CELL + 5 + bl + 1) * mm)
    p.lineTo(X(mx + 8) * mm, Y(sy * CELL + 5 + bl + 1) * mm)
    p.close()
    c.drawPath(p, stroke=0, fill=1)
    c.setStrokeColor(MOUSE); c.setLineWidth(1.3)   # axle line = the datum
    line(c, X(mx - bw / 2 - 8), Y(axle), X(mx + bw / 2 + 8), Y(axle))
    c.setFillColor(MOUSE)
    c.circle(X(mx) * mm, Y(axle) * mm, 1.3 * mm, stroke=0, fill=1)

    # --- numbered callouts, keyed underneath -------------------------------
    for i, (px, py, label, dx, dy) in enumerate(notes["marks"]):
        c.setFillColor(PAPER); c.setStrokeColor(PATH); c.setLineWidth(0.9)
        dash(c, [])
        c.circle((X(px) + dx) * mm, (Y(py) + dy) * mm, 2.7 * mm, stroke=1, fill=1)
        text(c, X(px) + dx, Y(py) + dy - 1.3, str(i + 1), 7.5, PATH,
             "Helvetica-Bold", "c")
        if dx or dy:
            c.setStrokeColor(colors.HexColor("#e8a3a1")); c.setLineWidth(0.5)
            k = 2.7 / max(1e-6, (dx * dx + dy * dy) ** 0.5)
            line(c, X(px) + dx - dx * k, Y(py) + dy - dy * k, X(px), Y(py))

    # --- the words ---------------------------------------------------------
    ty = 92
    text(c, 15, ty, notes["head"], 9.5, INK, "Helvetica-Bold")
    for i, (px, py, label, dx, dy) in enumerate(notes["marks"]):
        yy = ty - 7 - i * 4.6
        c.setFillColor(PAPER); c.setStrokeColor(PATH); c.setLineWidth(0.8)
        c.circle(17 * mm, (yy + 1.1) * mm, 2.3 * mm, stroke=1, fill=1)
        text(c, 17, yy, str(i + 1), 6.8, PATH, "Helvetica-Bold", "c")
        text(c, 22, yy, label, 7.8, INK)

    # --- the thing the turn table does not say ----------------------------
    by = ty - 13 - len(notes["marks"]) * 4.6
    c.setStrokeColor(colors.HexColor("#e0e3e7")); c.setLineWidth(0.6)
    c.setFillColor(colors.HexColor("#f7f8fa"))
    c.rect(15 * mm, (by - 26) * mm, 180 * mm, 30 * mm, stroke=1, fill=1)
    text(c, 20, by, "Read this off the drawing before you trust the table", 8.5,
         INK, "Helvetica-Bold")
    for i, ln in enumerate([
        "The straight lane and the diagonal cross at the WALL MIDPOINT \u2014 90 mm before the centre of the cell the SD45 is booked in.",
        "entry_offset is measured back from that CELL CENTRE, so an SD45 needs 90 + R\u00b7tan(22.5\u00b0) to be tangent to both lanes.",
        "At \u03c9 95 that is 90 + 75 = 165 mm. The table holds 120, which starts the arc 30 mm before the crossing instead of 75.",
        "DS45 and DD90 turn AT their lattice point, so those need R\u00b7tan(\u03b8/2) with no 90 added \u2014 75 and 63. Only SD45 carries the half cell.",
    ]):
        text(c, 20, by - 6 - i * 4.4, ln, 7.4, CENTRE)


# --------------------------------------------------------------------------
def rig_right():
    """bench-3x3-45minR: start (0,0) facing N, corridor NEN, SD45_R then DS45_L."""
    lane = HALF                                  # x = 90
    axle = HALF - BACK_WALL_TO_CENTER            # y = 41
    cross1 = (HALF, CELL)                        # (90,180)  straight meets diagonal
    cross2 = (CELL + HALF, 2 * CELL)             # (270,360) diagonal meets straight
    finish = (CELL + HALF, 2 * CELL + HALF)      # (270,450) cell (1,2) centre
    return {
        "path": [
            ("d", lane, axle, lane, cross1[1]),
            ("d", cross1[0], cross1[1], cross2[0], cross2[1]),
            ("d", cross2[0], cross2[1], finish[0], finish[1]),
        ],
        "marks": [
            (lane, axle, "Put her down here, backed square to the wall, facing north. Row 7 (SD45R), approach 1 cell.", -22, 0),
            (cross1[0], cross1[1], "SD45_R joins the diagonal HERE — the wall midpoint, 90 mm before the cell centre above it.", -13, 8),
            (cross2[0], cross2[1], "One diagonal step later, 127.279 mm, DS45_L puts her back on a straight lane.", 13, 8),
            (finish[0], finish[1], "She stops on this cell centre. Watch: did she land ON the blue line, and leave it square?", 13, 0),
        ],
        "head": "bench-3x3-45minR — right hand onto the diagonal, left hand off it",
        "body": [
            "Back her against the south wall of the bottom-left cell, facing north. Row 7 (SD45R), approach 1 cell.",
            "She runs up the centreline and joins the diagonal at the WALL MIDPOINT, 90 mm before the cell centre above her —",
            "not at the cell centre. One diagonal step of 127.279 mm later she leaves it again, and stops on the centre of the",
            "cell two up and one across. Watch two things: does she land ON the blue line, and does she leave it square.",
        ],
    }


def rig_left():
    """bench-3x3-45minL: start (2,0) facing N, corridor NWN, SD45_L then DS45_R."""
    lane = 2 * CELL + HALF                       # x = 450
    axle = HALF - BACK_WALL_TO_CENTER            # y = 41
    cross1 = (lane, CELL)                        # (450,180)
    cross2 = (CELL + HALF, 2 * CELL)             # (270,360)
    finish = (CELL + HALF, 2 * CELL + HALF)      # (270,450)
    return {
        "path": [
            ("d", lane, axle, lane, cross1[1]),
            ("d", cross1[0], cross1[1], cross2[0], cross2[1]),
            ("d", cross2[0], cross2[1], finish[0], finish[1]),
        ],
        "marks": [
            (lane, axle, "Put her down here \u2014 the bottom-RIGHT cell, backed to the wall, facing north. Row 6 (SD45L).", 22, 0),
            (cross1[0], cross1[1], "SD45_L joins the diagonal at the wall midpoint, exactly as its mirror does.", 13, 8),
            (cross2[0], cross2[1], "DS45_R leaves it one step later. Same distances, other hand.", -13, 8),
            (finish[0], finish[1], "Same finishing cell as the right-hand rig. Any difference between the two is the mouse.", -13, 0),
        ],
        "head": "bench-3x3-45minL — the mirror. Left hand onto the diagonal, right hand off it",
        "body": [
            "Same section, other corner: back her against the south wall of the bottom-RIGHT cell, facing north. Row 6 (SD45L).",
            "(2,0) facing north is the mirror image of (0,0) facing north, which is the only reason a 3×3 can show a left-hand",
            "45 at all. Every distance is identical to the right-hand rig; only the hand changes. Run the pair back to back and",
            "any difference between them is the mouse, not the geometry.",
        ],
    }


# --------------------------------------------------------------------------
def page_numbers(c, title):
    c.setFillColor(CENTRE); c.setFont("Helvetica", 7)
    c.drawString(15 * mm, 6 * mm, "E4 · bench diagnostics")
    c.drawRightString(195 * mm, 6 * mm, title)


def main(path):
    c = canvas.Canvas(path, pagesize=A4)
    c.setTitle("E4 bench diagnostics — cell template and the 45 tuning rigs")

    page_cell(c)
    page_numbers(c, "1:1 cell template")
    c.showPage()

    page_rig(c, "Where she goes — right-hand 45", (0, 0), "NEN", "R", rig_right())
    page_numbers(c, "bench-3x3-45minR")
    c.showPage()

    page_rig(c, "Where she goes — left-hand 45", (2, 0), "NWN", "L", rig_left())
    page_numbers(c, "bench-3x3-45minL")
    c.showPage()

    c.save()


if __name__ == "__main__":
    import sys
    main(sys.argv[1] if len(sys.argv) > 1 else "e4-bench-diagnostics.pdf")
    print("written")
