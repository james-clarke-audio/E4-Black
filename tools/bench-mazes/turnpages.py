#!/usr/bin/env python3
"""One printable page per turn: the single turn, and the line to judge it by.

A page showing two turns at once is a check, not a measurement -- a lateral
error out of the first moves the start of the second, so the finish only tells
you their sum. Each page here takes one row of the turn table on its own.

Everything on the page is computed from that row's own numbers:

    R = v / omega                  (omega in rad/s) -- the instantaneous radius
    t = R * tan(theta/2)           tangent length, either side of the crossing

An arc joining two lanes that cross at theta is tangent to both only if it
begins t before the crossing and ends t after. If it begins d before instead,
it finishes this far off the far lane:

    miss = (d - t) * sin(theta)          ... (d - t)/sqrt(2) at 45, (d - t) at 90

`entry_offset` is measured back from the LATTICE POINT the planner books the
turn at, which for most turns is also the crossing. Not for SD45/SD135: those
are booked in at a CELL CENTRE while a straight lane meets a diagonal at the
WALL MIDPOINT half a cell earlier, so those rows carry 90 mm on top of t.

The 180 is a different animal and gets its own arithmetic: there is no
crossing, and what has to come out right is the LATERAL step, 2R = 180 mm.
That fixes omega at 191 deg/s and leaves the offset controlling only how far
up the cell she does it.
"""
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.lib import colors
from reportlab.pdfgen import canvas
import math

CELL, HALF, POST = 180.0, 90.0, 12.0
DIAG = 90.0 * math.sqrt(2.0)
BACK_WALL_TO_CENTER = 49.0
SQ2 = math.sqrt(2.0)

INK    = colors.HexColor("#111111")
FAINT  = colors.HexColor("#ced3d8")
CENTRE = colors.HexColor("#6b7280")
RACE   = colors.HexColor("#1558d6")
GOOD   = colors.HexColor("#1a7f37")
BAD    = colors.HexColor("#d1332e")
MOUSE  = colors.HexColor("#2f6f3e")
PAPER  = colors.HexColor("#ffffff")

NORTH, EAST, SOUTH, WEST = 0, 1, 2, 3
DX = {NORTH: 0, EAST: 1, SOUTH: 0, WEST: -1}
DY = {NORTH: 1, EAST: 0, SOUTH: -1, WEST: 0}
LOOK = {"N": NORTH, "E": EAST, "S": SOUTH, "W": WEST}

NE = (1 / SQ2, 1 / SQ2)
NW = (-1 / SQ2, 1 / SQ2)
NN = (0.0, 1.0)

# The turn table as src/mouse.cpp holds it. Speed and alpha are the same on
# every row today, so they live here once.
V, ALPHA = 300, 2500
TURNS = {
    0:  ("SS90EL", 100,  90.0, 170), 1:  ("SS90ER", 100, -90.0, 170),
    2:  ("SS90L",  100,  90.0, 170), 3:  ("SS90R",  100, -90.0, 170),
    4:  ("SS180L",  90, 180.0, 191), 5:  ("SS180R",  90, -180.0, 191),
    6:  ("SD45L",  120,  45.0,  95), 7:  ("SD45R",  120, -45.0,  95),
    8:  ("DS45L",  120,  45.0,  95), 9:  ("DS45R",  120, -45.0,  95),
    14: ("DD90L",   63,  90.0, 273), 15: ("DD90R",   63, -90.0, 273),
}


# --------------------------------------------------------------------------
def line(c, x1, y1, x2, y2):
    c.line(x1 * mm, y1 * mm, x2 * mm, y2 * mm)


def text(c, x, y, s, size=8, col=INK, font="Helvetica", anchor="l"):
    c.setFillColor(col); c.setFont(font, size)
    (c.drawCentredString if anchor == "c" else
     c.drawRightString if anchor == "r" else c.drawString)(x * mm, y * mm, s)


def corridor_walls(start, walk, n=3):
    walls = {(x, y): {NORTH, EAST, SOUTH, WEST} for x in range(n) for y in range(n)}
    x, y = start
    for ch in walk:
        d = LOOK[ch]
        walls[(x, y)].discard(d)
        x, y = x + DX[d], y + DY[d]
        walls[(x, y)].discard((d + 2) % 4)
    return walls


# --------------------------------------------------------------------------
def page_turn(c, s):
    row = s["row"]
    name, entry, ang, om = TURNS[row]
    theta = abs(ang)
    R = V / (om * math.pi / 180.0)
    right = ang < 0
    is180 = theta >= 180

    cx0, cy0, cw, ch = s["window"]
    # 180 mm of page width is the most there is, but a tall window has to give
    # some of it back or the numbers below it fall off the bottom of the sheet.
    draw_w = min(180.0, 150.0 * cw / float(ch))
    scale = draw_w / (cw * CELL)
    draw_h = ch * CELL * scale
    ox = (210.0 - draw_w) / 2.0
    oy = 276.0 - draw_h

    def X(u): return ox + (u - cx0 * CELL) * scale
    def Y(v): return oy + (v - cy0 * CELL) * scale

    cross_off = s.get("crossing_offset", 0.0)
    agreed = False
    t = R * math.tan(math.radians(theta) / 2) if not is180 else 0.0
    need = cross_off + t
    d_now = entry - cross_off
    miss = (d_now - t) * math.sin(math.radians(theta)) if not is180 else 0.0
    late = d_now < t
    agreed = (not is180) and abs(entry - (cross_off + t)) < 1.0

    text(c, 105, 288, "Row %d — %s, %s" % (row, name, s["what"]),
         14.5, INK, "Helvetica-Bold", "c")
    text(c, 105, 281.5, s["sub"], 8.5, CENTRE, "Helvetica", "c")

    # --- the lattice -------------------------------------------------------
    for i in range(cw):
        for j in range(ch):
            bx, by = (cx0 + i) * CELL, (cy0 + j) * CELL
            c.setStrokeColor(FAINT); c.setLineWidth(0.35); c.setDash([])
            line(c, X(bx), Y(by), X(bx + CELL), Y(by + CELL))
            line(c, X(bx + CELL), Y(by), X(bx), Y(by + CELL))
            c.setStrokeColor(CENTRE); c.setLineWidth(0.4); c.setDash([2, 2])
            line(c, X(bx + HALF), Y(by), X(bx + HALF), Y(by + CELL))
            line(c, X(bx), Y(by + HALF), X(bx + CELL), Y(by + HALF))
            c.setDash([])
            c.setStrokeColor(RACE); c.setLineWidth(0.5)
            m = [(bx + HALF, by), (bx + CELL, by + HALF),
                 (bx + HALF, by + CELL), (bx, by + HALF)]
            for k in range(4):
                a, b = m[k], m[(k + 1) % 4]
                line(c, X(a[0]), Y(a[1]), X(b[0]), Y(b[1]))

    # --- walls -------------------------------------------------------------
    walls = corridor_walls(s["start"], s["walk"])
    c.setStrokeColor(INK); c.setLineWidth(2.2); c.setDash([])
    for i in range(cw):
        for j in range(ch):
            cell = (cx0 + i, cy0 + j)
            w = walls.get(cell, set())
            bx, by = cell[0] * CELL, cell[1] * CELL
            if NORTH in w: line(c, X(bx), Y(by + CELL), X(bx + CELL), Y(by + CELL))
            if SOUTH in w: line(c, X(bx), Y(by), X(bx + CELL), Y(by))
            if EAST  in w: line(c, X(bx + CELL), Y(by), X(bx + CELL), Y(by + CELL))
            if WEST  in w: line(c, X(bx), Y(by), X(bx), Y(by + CELL))
    c.setFillColor(INK)
    for i in range(cw + 1):
        for j in range(ch + 1):
            px, py = (cx0 + i) * CELL, (cy0 + j) * CELL
            c.rect((X(px) - 2.0) * mm, (Y(py) - 2.0) * mm, 4.0 * mm, 4.0 * mm,
                   stroke=0, fill=1)

    # --- the lanes ---------------------------------------------------------
    (a1, a2) = s["lane_in"]
    c.setStrokeColor(CENTRE); c.setLineWidth(1.0); c.setDash([5, 3])
    line(c, X(a1[0]), Y(a1[1]), X(a2[0]), Y(a2[1]))
    c.setDash([])
    (b1, b2) = s["lane_out"]
    c.setStrokeColor(RACE); c.setLineWidth(2.6)
    line(c, X(b1[0]), Y(b1[1]), X(b2[0]), Y(b2[1]))

    # --- the arcs ----------------------------------------------------------
    crx, cry = s["crossing"]
    hx, hy = s["heading"]
    nx, ny = (hy, -hx) if right else (-hy, hx)

    def arc(start_d, col, width, dashed):
        sx, sy = crx - hx * start_d, cry - hy * start_d
        Cx, Cy = sx + nx * R, sy + ny * R
        a0 = math.degrees(math.atan2(sy - Cy, sx - Cx))
        st = (a0 - theta) if right else a0
        c.setStrokeColor(col); c.setLineWidth(width)
        c.setDash([3, 2] if dashed else [])
        c.arc(X(Cx - R) * mm, Y(Cy - R) * mm, X(Cx + R) * mm, Y(Cy + R) * mm, st, theta)
        c.setDash([])
        a1_ = math.radians(a0 - theta if right else a0 + theta)
        return (sx, sy), (Cx + R * math.cos(a1_), Cy + R * math.sin(a1_))

    ux, uy = b2[0] - b1[0], b2[1] - b1[1]
    un = math.hypot(ux, uy); ux, uy = ux / un, uy / un

    def clip(px, py):
        lo, hi = cx0 * CELL, (cx0 + cw) * CELL
        bo, to = cy0 * CELL, (cy0 + ch) * CELL
        best = 1e9
        for (val, o, dd) in ((hi, px, ux), (lo, px, ux), (to, py, uy), (bo, py, uy)):
            if abs(dd) > 1e-9:
                k = (val - o) / dd
                if k > 0:
                    best = min(best, k)
        return max(0.0, best - 5.0)

    if is180:
        # Nothing to compare against: at omega 191 the arc lands on the next
        # lane by construction. Draw the one arc she should make.
        (s_ok, e_ok) = arc(entry, GOOD, 2.2, False)
        s_now = e_now = None
    elif abs(entry - need) < 2.0:
        # This row already agrees with itself. Drawing a second arc 1 mm away
        # would be a lie about how much there is to find here.
        (s_ok, e_ok) = arc(t, GOOD, 2.2, False)
        s_now = e_now = None
    else:
        (s_ok, e_ok) = arc(t, GOOD, 2.2, False)
        (s_now, e_now) = arc(d_now, BAD, 1.6, True)
        k_no = clip(*e_now)
        c.setStrokeColor(BAD); c.setLineWidth(1.6); c.setDash([3, 2])
        line(c, X(e_now[0]), Y(e_now[1]), X(e_now[0] + ux * k_no),
             Y(e_now[1] + uy * k_no))
        c.setDash([])

    # --- lead-in -----------------------------------------------------------
    dx0, dy0 = s["datum"]
    c.setStrokeColor(GOOD); c.setLineWidth(2.2)
    line(c, X(dx0), Y(dy0), X(s_ok[0]), Y(s_ok[1]))
    if s_now:
        c.setStrokeColor(BAD); c.setLineWidth(1.6); c.setDash([3, 2])
        line(c, X(dx0), Y(dy0), X(s_now[0]), Y(s_now[1]))
        c.setDash([])

    # --- the mouse, oriented -----------------------------------------------
    bw, bl = 76.0, 92.0
    mhx, mhy = s.get("mouse_heading", s["heading"])
    c.saveState()
    c.translate(X(dx0) * mm, Y(dy0) * mm)
    c.rotate(math.degrees(math.atan2(mhy, mhx)) - 90.0)
    c.setStrokeColor(MOUSE); c.setLineWidth(1.0)
    c.setFillColor(colors.HexColor("#dfeade"))
    c.roundRect((-bw / 2) * scale * mm, (-0.4 * bl) * scale * mm,
                bw * scale * mm, bl * scale * mm, 2.2 * mm, stroke=1, fill=1)
    c.setFillColor(MOUSE)
    p = c.beginPath()
    p.moveTo(0, (0.6 * bl + 10) * scale * mm)
    p.lineTo(-8 * scale * mm, (0.6 * bl + 1) * scale * mm)
    p.lineTo(8 * scale * mm, (0.6 * bl + 1) * scale * mm)
    p.close()
    c.drawPath(p, stroke=0, fill=1)
    c.setStrokeColor(MOUSE); c.setLineWidth(1.2)
    c.line((-bw / 2 - 8) * scale * mm, 0, (bw / 2 + 8) * scale * mm, 0)
    c.restoreState()

    # --- the crossing ------------------------------------------------------
    c.setStrokeColor(INK); c.setLineWidth(0.9); c.setFillColor(PAPER)
    c.circle(X(crx) * mm, Y(cry) * mm, 2.0 * mm, stroke=1, fill=1)
    c.setFillColor(INK)
    c.circle(X(crx) * mm, Y(cry) * mm, 0.8 * mm, stroke=0, fill=1)

    # --- the miss ----------------------------------------------------------
    if s_now and abs(miss) > 1.0:
        gm = min(clip(*e_ok), clip(*e_now)) * 0.7
        gx, gy = e_ok[0] + ux * gm, e_ok[1] + uy * gm
        px, py = -uy, ux
        c.setStrokeColor(BAD); c.setLineWidth(0.8)
        line(c, X(gx), Y(gy), X(gx + px * -miss), Y(gy + py * -miss))
        text(c, X(gx + px * -miss * 0.5) + s.get("miss_dx", -3),
             Y(gy + py * -miss * 0.5) + s.get("miss_dy", 3.5),
             "%.0f mm off the lane" % abs(miss), 7.5, BAD, "Helvetica-Bold",
             s.get("miss_anchor", "r"))

    # --- callouts ----------------------------------------------------------
    for (px_, py_, lab, dx, dy, col) in s["marks"]:
        c.setFillColor(PAPER); c.setStrokeColor(col); c.setLineWidth(0.9)
        c.circle((X(px_) + dx) * mm, (Y(py_) + dy) * mm, 2.6 * mm, stroke=1, fill=1)
        text(c, X(px_) + dx, Y(py_) + dy - 1.2, lab, 7.3, col, "Helvetica-Bold", "c")
        if dx or dy:
            c.setStrokeColor(colors.HexColor("#c9c9c9")); c.setLineWidth(0.5)
            k = 2.6 / math.hypot(dx, dy)
            line(c, X(px_) + dx * (1 - k), Y(py_) + dy * (1 - k), X(px_), Y(py_))

    # --- key ---------------------------------------------------------------
    ky = oy - 9

    def swatch(x, col, w, dsh, label):
        c.setStrokeColor(col); c.setLineWidth(w); c.setDash(dsh)
        line(c, x, ky + 1.1, x + 11, ky + 1.1)
        c.setDash([])
        text(c, x + 13.5, ky, label, 7.2, CENTRE)

    swatch(17, RACE, 2.6, [], "the lane — measure to this")
    swatch(77, GOOD, 2.2, [], "the turn it should drive")
    if s_now:
        swatch(133, BAD, 1.6, [3, 2], "what the table drives today")

    # --- numbers -----------------------------------------------------------
    lead_now = s["base"] + s["approach"] * s["pitch"] - entry
    lead_need = s["base"] + s["approach"] * s["pitch"] - need
    ty = oy - 20
    cmd_entry = entry if is180 else need
    text(c, 15, ty, "SEL,%d    POS,%d    ARC,%d,%g,%d,%d,%.0f,90%s"
         % (row, s["approach"], V, ang, om, ALPHA, cmd_entry,
            "" if is180 or abs(need - entry) < 1 else
            "        — the table holds %d; this line corrects it" % entry),
         9.5, INK, "Helvetica-Bold")

    if is180:
        rows = [
            ("commanded speed", "%d mm/s" % V, ""),
            ("omega", "%d deg/s" % om, "and this one is NOT free — see below"),
            ("R = v / omega", "%.0f mm" % R, "instantaneous; alpha's ramps make the real arc wider"),
            ("lateral step  2R", "%.0f mm" % (2 * R), "has to be 180 — one cell — or she lands off the lane"),
            ("out by", "%.0f mm" % round(2 * R - CELL),
             "at the lane she is aiming for" if abs(2 * R - CELL) >= 1
             else "\u2014 it lands on the lane"),
            ("entry_offset", "%d mm" % entry, "sets only how far up the cell she does it"),
            ("lead-in it drives", "%.0f mm" % lead_now, "%s, then the arc" % s["from"]),
        ]
    else:
        rows = [
            ("commanded speed", "%d mm/s" % V, ""),
            ("omega", "%d deg/s" % om, "the only thing that sets the radius"),
            ("R = v / omega", "%.0f mm" % R, "instantaneous; alpha's ramps make the real arc a little wider"),
            ("tangent  R·tan(%g°)" % (theta / 2.0), "%.0f mm" % t,
             "either side of where the lanes cross"),
            ("+ half cell", "%.0f mm" % cross_off, s["crossing_note"]),
            ("entry_offset it needs", "%.0f mm" % need, "so the arc is tangent to both lanes"),
            ("entry_offset it has", "%d mm" % entry,
             "agrees" if agreed else "out by %+.0f mm" % (entry - need)),
            ("lead-in it drives now", "%.0f mm" % lead_now, "%s, then the arc" % s["from"]),
        ]
        if not agreed:
            rows.append(("lead-in once corrected", "%.0f mm" % lead_need,
                         "she starts turning %.0f mm %s"
                         % (abs(need - entry), "earlier" if need > entry else "later")))
    for i, (k, val, note) in enumerate(rows):
        yy = ty - 7 - i * 5.0
        hot = ("out by" in note or k == "out by") and abs(entry - need) >= 5
        hot = hot or (k == "out by" and abs(2 * R - CELL) >= 5)
        text(c, 17, yy, k, 8, CENTRE)
        text(c, 78, yy, val, 8, BAD if hot else INK, "Helvetica-Bold", "r")
        text(c, 83, yy, note, 7.4, BAD if hot else CENTRE)

    # --- what to read ------------------------------------------------------
    by = ty - 14 - len(rows) * 5.0
    def wrap(t_, n=138):
        out, cur = [], ""
        for w in t_.split(" "):
            trial = (cur + " " + w).strip()
            if len(trial) > n:
                out.append(cur); cur = w
            else:
                cur = trial
        if cur:
            out.append(cur)
        return out

    body = list(s["read"])
    if not is180:
        if abs(entry - need) < 2.0:
            body.append("This row already agrees with itself: the offset it has and the offset the radius "
                        "wants are the same number, so there is no drawn error to chase. What is left is the floor.")
        else:
            body.append(
                ("Too LATE (offset too small) and she overshoots to the far side of the line, away from the post; "
                 "too EARLY and she cuts inside it, close to that post. "
                 + ("She is LATE today, by %.0f mm of offset." if late else
                    "She is EARLY today, by %.0f mm of offset.") % abs(entry - need)))
        body.append(
            "1 mm of entry offset moves her %.2f mm. 1 deg/s of omega moves her %.2f mm. "
            "Trim the OFFSET: omega sets the radius, and the radius has to fit the space."
            % (math.sin(math.radians(theta)),
               (R / om) * (1 - math.cos(math.radians(theta)))))
        body = [w for ln in body for w in wrap(ln)]
    h = 7.0 + len(body) * 4.3
    c.setStrokeColor(colors.HexColor("#e0e3e7")); c.setLineWidth(0.6)
    c.setFillColor(colors.HexColor("#f7f8fa"))
    c.rect(15 * mm, (by + 4 - h) * mm, 180 * mm, h * mm, stroke=1, fill=1)
    text(c, 20, by, "What to read off the floor", 8.5, INK, "Helvetica-Bold")
    for i, ln in enumerate(body):
        text(c, 20, by - 5.5 - i * 4.3, ln, 7.4, CENTRE)

    c.setFillColor(CENTRE); c.setFont("Helvetica", 7)
    c.drawString(15 * mm, 6 * mm, "E4 · turn tuning · one row per page")
    c.drawRightString(195 * mm, 6 * mm, name)
    c.showPage()


# ==========================================================================
#  the pages
# ==========================================================================
def ss90(row, hand):
    """A single 90 out of a straight, into a straight. The crossing is the cell
    centre, so the offset is the tangent and nothing else."""
    r = hand == "R"
    start = (0, 0) if r else (2, 0)
    lane = HALF if r else 2 * CELL + HALF
    cross = (lane, CELL + HALF)
    out_end = (3 * CELL - 6, CELL + HALF) if r else (6, CELL + HALF)
    speed = TURNS[row][0].startswith("SS90") and not TURNS[row][0].startswith("SS90E")
    return {
        "row": row, "window": (0, 0, 3, 2),
        "start": start, "walk": "NEE" if r else "NWW",
        "what": "a 90 out of a straight",
        "sub": ("the speed-run 90 — this one has to chain, wall midpoint to wall midpoint"
                if speed else
                "the search 90 — she stops and senses either side of it, so it need not chain"),
        "crossing": cross, "crossing_offset": 0.0,
        "heading": NN, "datum": (lane, HALF - BACK_WALL_TO_CENTER),
        "lane_in": ((lane, 0), (lane, CELL + HALF + 30)),
        "lane_out": (cross, out_end),
        "approach": 1, "pitch": CELL, "base": BACK_WALL_TO_CENTER,
        "from": "out of the start cell",
        "crossing_note": "two straight lanes cross AT the cell centre, so nothing is added",
        "marks": [
            ((lane), HALF - BACK_WALL_TO_CENTER, "1", (-24 if r else 24), 0, MOUSE),
            (cross[0], cross[1], "2", (-18 if r else 18), 14, INK),
            (cross[0] + (150 if r else -150), cross[1], "3", 0, 16, RACE),
        ],
        "miss_anchor": "l" if r else "r", "miss_dx": 3 if r else -3,
        "read": [
            "1   Back her against the wall, facing north. 2   The two lanes cross at the CELL CENTRE for a 90 — no half cell here.",
            "3   Coming out she should be on the blue line and square to it. Measure the wall gap either side; they should match.",
        ],
    }


def ss180(row, hand):
    r = hand == "R"
    start = (0, 0) if r else (2, 0)
    lane = HALF if r else 2 * CELL + HALF
    out_lane = lane + (CELL if r else -CELL)
    cross = (lane, CELL + HALF)
    return {
        "row": row, "window": (0, 0, 3, 2),
        "start": start, "walk": "NES" if r else "NWS",
        "what": "the hairpin",
        "sub": "not a dead end — she comes back down the NEXT lane, so the lateral step has to be exactly one cell",
        "crossing": cross, "crossing_offset": 0.0,
        "heading": NN, "datum": (lane, HALF - BACK_WALL_TO_CENTER),
        "lane_in": ((lane, 0), (lane, CELL + HALF + 20)),
        "lane_out": ((out_lane, CELL), (out_lane, 6)),
        "approach": 1, "pitch": CELL, "base": BACK_WALL_TO_CENTER,
        "from": "out of the start cell",
        "crossing_note": "",
        "marks": [
            (lane, HALF - BACK_WALL_TO_CENTER, "1", (-24 if r else 24), 0, MOUSE),
            (cross[0], cross[1], "2", (-20 if r else 20), 6, INK),
            (out_lane, HALF, "3", (20 if r else -20), 0, RACE),
        ],
        "read": [
            "1   Back her against the wall, facing north. 2   entry_offset is measured back from this cell centre; the arc starts 90 mm before it.",
            "3   She must come back down the blue line — the next lane over, 180 mm across. Measure the wall gap on both sides.",
            "OMEGA IS NOT FREE ON THIS ROW. A 180 of radius R steps her sideways by 2R, and that has to be exactly one cell.",
            "2R = 180 forces R = 90 and omega = 191 deg/s at 300 mm/s. 1 deg/s moves her 0.94 mm; the offset moves her not at all.",
        ],
    }


def sd45(row, hand):
    """Straight onto the diagonal. The one family that carries the half cell."""
    r = hand == "R"
    start = (0, 0) if r else (2, 0)
    lane = HALF if r else 2 * CELL + HALF
    cross = (lane, CELL)
    end = (CELL + HALF, 2 * CELL) if r else (CELL + HALF, 2 * CELL)
    return {
        "row": row, "window": (0, 0, 2, 2) if r else (1, 0, 2, 2),
        "start": start, "walk": "NEN" if r else "NWN",
        "what": "straight onto the diagonal",
        "sub": "one turn on its own — the heavy blue line is the thing she has to end up on",
        "crossing": cross, "crossing_offset": 90.0,
        "heading": NN, "datum": (lane, HALF - BACK_WALL_TO_CENTER),
        "lane_in": ((lane, 0), (lane, CELL + 20)),
        "lane_out": (cross, end),
        "approach": 1, "pitch": CELL, "base": BACK_WALL_TO_CENTER,
        "from": "out of the start cell",
        "crossing_note": "a straight lane meets a diagonal at the WALL MIDPOINT, not the cell centre",
        "marks": [
            (lane, HALF - BACK_WALL_TO_CENTER, "1", (-26 if r else 26), 0, MOUSE),
            (cross[0], cross[1], "2", (-20 if r else 20), 10, INK),
            ((CELL + 30) if r else (2 * CELL - 30), CELL + 120, "3",
             (18 if r else -18), -12, RACE),
        ],
        "miss_anchor": "r" if r else "l", "miss_dx": -3 if r else 3,
        "read": [
            "1   Back her against the wall, facing north. Nothing else in the section matters for this run.",
            "2   She should already be turning by the time she reaches the wall midpoint — that, not the cell centre, is where the lanes cross.",
            "3   Coming out of the arc she should be ON the blue line and parallel to it. That is the whole measurement; ignore where she stops.",
        ],
    }


def ds45(row, hand):
    """Diagonal back onto a straight. She is PLACED on the diagonal by hand."""
    r = hand == "R"                       # DS45_R leaves a NW diagonal
    cross = (CELL + HALF, 2 * CELL)       # (270,360) -- a wall midpoint
    datum = (2 * CELL, CELL + HALF) if r else (CELL, CELL + HALF)
    head = NW if r else NE
    lane_in_start = (2 * CELL + HALF, CELL) if r else (HALF, CELL)
    return {
        "row": row,
        "window": (1, 1, 2, 2) if r else (0, 1, 2, 2),
        "start": (2, 0) if r else (0, 0), "walk": "NWN" if r else "NEN",
        "what": "diagonal back onto a straight",
        "sub": "she is placed ON the diagonal for this one — there is no wall to back up against",
        "crossing": cross, "crossing_offset": 0.0,
        "heading": head, "mouse_heading": head, "datum": datum,
        "lane_in": (lane_in_start, cross),
        "lane_out": (cross, (CELL + HALF, 3 * CELL - 6)),
        "approach": 1, "pitch": DIAG, "base": 0.0,
        "from": "along the diagonal",
        "crossing_note": "she turns AT her lattice point here, so nothing is added",
        "marks": [
            (datum[0], datum[1], "1", (22 if r else -22), -10, MOUSE),
            (cross[0], cross[1], "2", (20 if r else -20), -8, INK),
            (CELL + HALF, 2 * CELL + 110, "3", (20 if r else -20), 0, RACE),
        ],
        "miss_anchor": "l", "miss_dx": 3,
        "read": [
            "1   Put her down ON the blue diagonal, one step back from the turn, squared to it by eye against the line.",
            "2   The diagonal and the straight lane cross AT the wall midpoint, which is also the point the offset is measured from.",
            "3   Coming out she should be on the centreline of the column above, and square to it.",
        ],
    }


def dd90(row, hand):
    """Diagonal to diagonal, at a wall midpoint. Nothing else turns here."""
    r = hand == "R"                       # DD90_R: NW in, NE out
    cross = (CELL + HALF, 2 * CELL)
    datum = (2 * CELL, CELL + HALF) if r else (CELL, CELL + HALF)
    head = NW if r else NE
    lane_in_start = (2 * CELL + HALF, CELL) if r else (HALF, CELL)
    out_end = (2 * CELL + HALF, 3 * CELL - 6) if r else (HALF, 3 * CELL - 6)
    return {
        "row": row,
        "window": (1, 1, 2, 2) if r else (0, 1, 2, 2),
        "start": (2, 0) if r else (0, 0), "walk": "NWNE" if r else "NENW",
        "what": "the diagonal changes hand",
        "sub": "the only turn that begins and ends on a diagonal — and the only one with 127 mm, not 180, either side of it",
        "crossing": cross, "crossing_offset": 0.0,
        "heading": head, "mouse_heading": head, "datum": datum,
        "lane_in": (lane_in_start, cross),
        "lane_out": (cross, out_end),
        "approach": 1, "pitch": DIAG, "base": 0.0,
        "from": "along the diagonal",
        "crossing_note": "two diagonals cross AT the wall midpoint, so nothing is added",
        "marks": [
            (datum[0], datum[1], "1", (22 if r else -22), -10, MOUSE),
            (cross[0], cross[1], "2", (22 if r else -22), 0, INK),
            (CELL + HALF + (60 if r else -60), 2 * CELL + 60, "3",
             (18 if r else -18), 8, RACE),
        ],
        "miss_anchor": "l" if r else "r", "miss_dx": 3 if r else -3,
        "read": [
            "1   Put her down ON the blue diagonal, one step back, squared to it against the line.",
            "2   She turns at the wall midpoint, and the step either side is 127.279 mm — not 180. That is what makes this the tight one.",
            "3   Coming out she should be on the OTHER blue diagonal and parallel to it, having passed the post without touching it.",
        ],
    }


def page_contents(c, specs):
    """Every row on one sheet, so the bench has a running order."""
    text(c, 105, 282, "E4 \u2014 turn tuning", 16, INK, "Helvetica-Bold", "c")
    text(c, 105, 274.5,
         "one page per row. Every number below is derived from the row itself \u2014 nothing here was measured.",
         8.5, CENTRE, "Helvetica", "c")

    cols = [(18, "row", "l"), (28, "name", "l"), (62, "\u03c9", "r"),
            (80, "R", "r"), (99, "tangent", "r"), (119, "+\u00bd cell", "r"),
            (138, "needs", "r"), (154, "has", "r"), (176, "out by", "r"),
            (196, "page", "r")]
    y = 262
    c.setStrokeColor(colors.HexColor("#d7dbe0")); c.setLineWidth(0.6)
    line(c, 15, y + 5.5, 195, y + 5.5)
    for (x, lab, an) in cols:
        text(c, x, y, lab, 7.2, CENTRE, "Helvetica-Bold", an)
    line(c, 15, y - 2.5, 195, y - 2.5)

    y -= 8
    for pg, sp in enumerate(specs, start=3):
        name, entry, ang, om = TURNS[sp["row"]]
        theta = abs(ang)
        R = V / (om * math.pi / 180.0)
        if theta >= 180:
            vals = ["%d" % sp["row"], name, "%d" % om, "%.0f" % R, "\u2014", "\u2014",
                    "\u2014", "%d" % entry, "%+.0f" % round(2 * R - CELL), "%d" % pg]
            hot = abs(2 * R - CELL) >= 5
        else:
            t = R * math.tan(math.radians(theta) / 2)
            co = sp.get("crossing_offset", 0.0)
            need = co + t
            vals = ["%d" % sp["row"], name, "%d" % om, "%.0f" % R, "%.0f" % t,
                    "%.0f" % co if co else "\u2014", "%.0f" % need, "%d" % entry,
                    "%+.0f" % (entry - need) if abs(entry - need) >= 1 else "agrees",
                    "%d" % pg]
            hot = abs(entry - need) >= 5
        for (x, _lab, an), v in zip(cols, vals):
            text(c, x, y, v, 7.6, BAD if hot else INK,
                 "Helvetica-Bold" if hot and an == "r" else "Helvetica", an)
        y -= 6.2

    line(c, 15, y + 3.5, 195, y + 3.5)
    y -= 4

    para = [
        ("Four of the twelve rows already agree with themselves. The 45s do not, and they cannot both be right "
         "while they share one number: an SD45 is booked in at a cell centre while its two lanes cross at the wall "
         "midpoint half a cell earlier, so it wants 165, and a DS45 turns at its own lattice point and wants 75. "
         "Both hold 120 today."),
        ("A running order that does not waste an afternoon: gyro first, then the 90s (rows 0\u20133) because they are "
         "the only rows that have ever met a floor and they are the check on everything else. Then SD45 and DS45 "
         "separately \u2014 never chained \u2014 because a lateral error out of the first moves the start of the second. "
         "Then DD90, which has 127 mm either side of it instead of 180 and is the tightest thing she does. The 180 "
         "last: its omega is forced by the cell pitch, so there is nothing to search for."),
        ("Trim the OFFSET, not omega. Omega sets the radius, and the radius has to fit the space it is turning in; "
         "the offset is free. Both move her, at the rates each page gives."),
        ("Each page carries the exact three lines to type into Turn tuning, with the corrected offset already in "
         "them. S saves the table to EEPROM; nothing is permanent until it does."),
    ]
    for t_ in para:
        words, lineb = t_.split(" "), ""
        for w in words:
            trial = (lineb + " " + w).strip()
            if len(trial) > 116:
                text(c, 18, y, lineb, 8, CENTRE); y -= 4.6; lineb = w
            else:
                lineb = trial
        text(c, 18, y, lineb, 8, CENTRE); y -= 8.0

    c.setFillColor(CENTRE); c.setFont("Helvetica", 7)
    c.drawString(15 * mm, 6 * mm, "E4 \u00b7 turn tuning \u00b7 one row per page")
    c.drawRightString(195 * mm, 6 * mm, "contents")
    c.showPage()


def page_135(c):
    """No drawing: the 135s do not currently have a geometry to draw."""
    text(c, 105, 275, "Rows 10–13 — the 135s", 15, INK, "Helvetica-Bold", "c")
    text(c, 105, 267, "why there is no page for them yet", 9, CENTRE, "Helvetica", "c")
    body = [
        ("A 135 turns a straight onto a diagonal the long way round, or back off it. The lanes still cross at a "
         "wall midpoint, so SD135 carries the same half cell an SD45 does. What it does not have is room."),
        ("The tangent of a 135 turn is R·tan(67.5°) = 2.414R — nearly six times an SD45's. At the table's "
         "omega of 191 deg/s, R is 90 mm and the tangent is 217 mm. Add the half cell and an SD135 would have to "
         "start turning 307 mm before the cell it is booked in at, which is most of two cells back, through a wall."),
        ("To make the tangent fit inside one cell the radius has to come down to about 37 mm, which at 300 mm/s is "
         "omega 464 deg/s. That is a spin with the wheels rolling, and the lateral acceleration goes with it."),
        ("So the 135s stay as slots. The planner does not emit them, nothing drives them, and the four rows in the "
         "table are arithmetic holding a place. When they are wanted, they want their own speed as well as their "
         "own radius — which is a different page from any of these."),
    ]
    y = 250
    for para in body:
        words, lineb = para.split(" "), ""
        for w in words:
            trial = (lineb + " " + w).strip()
            if len(trial) > 112:
                text(c, 18, y, lineb, 9, INK); y -= 5.2; lineb = w
            else:
                lineb = trial
        text(c, 18, y, lineb, 9, INK); y -= 9.5
    c.setFillColor(CENTRE); c.setFont("Helvetica", 7)
    c.drawString(15 * mm, 6 * mm, "E4 · turn tuning · one row per page")
    c.drawRightString(195 * mm, 6 * mm, "SD135 / DS135")
    c.showPage()


def main(path):
    from benchsheet import page_cell           # the 1:1 sheet, unchanged

    specs = [ss90(1, "R"), ss90(0, "L"), ss90(3, "R"), ss90(2, "L"),
             sd45(7, "R"), sd45(6, "L"),
             ds45(8, "L"), ds45(9, "R"),
             dd90(14, "L"), dd90(15, "R"),
             ss180(5, "R"), ss180(4, "L")]

    c = canvas.Canvas(path, pagesize=A4)
    c.setTitle("E4 \u2014 bench diagnostics and turn tuning")
    page_cell(c)                                # page 1: the only 1:1 sheet
    c.setFillColor(CENTRE); c.setFont("Helvetica", 7)
    c.drawString(15 * mm, 3 * mm, "E4 \u00b7 bench diagnostics")
    c.drawRightString(195 * mm, 3 * mm, "1:1 cell template")
    c.showPage()
    page_contents(c, specs)                     # page 2
    for spec in specs:                          # pages 3..14
        page_turn(c, spec)
    page_135(c)                                 # page 15
    c.save()


if __name__ == "__main__":
    import sys
    main(sys.argv[1] if len(sys.argv) > 1 else "e4-turn-pages.pdf")
    print("written")
