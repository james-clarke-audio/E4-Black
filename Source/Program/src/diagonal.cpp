#include "diagonal.h"
#include <math.h>

namespace plan {

// A zigzag is any alternation of 90 degree turns, ARC OR SPIN.
//
// Matching only arcs finds nothing, and the reason is worth knowing: at
// entry_offset 100 mm a smooth 90 eats 100 mm of the cell going in and 100
// coming out, so two of them one cell apart need 180 - 200 = -20 mm of straight
// between them. The planner already rejects that as impossible, so the zigzags
// in an orthogonal route are STOP-AND-SPIN sequences, not arcs -- and those are
// the ones a diagonal replaces most profitably, because every spin is a full
// stop.
static inline bool is_left90(Move m)  { return m == MV_ARC_L || m == MV_SPIN_L; }
static inline bool is_right90(Move m) { return m == MV_ARC_R || m == MV_SPIN_R; }
static inline bool is_turn90(Move m)  { return is_left90(m) || is_right90(m); }

// ---------------------------------------------------------------------------
// THE PARITY RULE, which this pass got wrong until a validator caught it.
//
// A diagonal runs through wall midpoints: in half-cells (u = x/90) those are
// the points with exactly one odd coordinate. Coming off the diagonal, the
// landing point is (u,v) + h, and it has to be a CELL CENTRE -- both odd. For
// one of the two 45 degree exits it is; for the other it lands on a post, and
// that turn is simply not one she can make. At a vertical-wall midpoint she is
// already on a row centreline but straddling a wall line, so only E or W are
// available; at a horizontal-wall midpoint only N or S.
//
// Substituting without this check produced routes that read 8-10% faster and
// could not be driven. The exit hand is now chosen by what the lattice allows
// rather than by which way the last turn of the zigzag went.
static const int LHX[4] = { 0, 1, 0, -1 };
static const int LHY[4] = { 1, 0, -1, 0 };
static const int LDX[4] = { 1, 1, -1, -1 };
static const int LDY[4] = { 1, -1, -1, 1 };

/// Where the diagonal actually ends, and which exit hand is legal there.
///
/// AND IT DOES NOT END WHERE THE ZIGZAG DID. That was the second bug the
/// validator caught, and it is the one that matters. A k-turn zigzag finishes
/// its last turn IN a cell; the diagonal that replaces it comes off the
/// lattice a cell or so further along, because the DS45 lands her at the next
/// centre rather than the one the last turn happened in. Splice it in without
/// noticing and the following straight is counted from the wrong place, so she
/// overshoots by exactly one cell and every wall check after it is against the
/// wrong cell. Nothing in the timing model can see that; only a walk of the
/// route against the map can.
///
/// Returns false if neither 45 is legal -- then the zigzag stays as it is.
static bool diag_exit(int x0, int y0, int h_in, bool first_left, int k,
                      bool &exit_left, int &ex, int &ey, int &h_out_o) {
  const int dd = first_left ? ((h_in + 3) & 3) : h_in;
  const int u = 2 * x0 + 1 + (LDX[dd] - LHX[h_in]) + (k - 1) * LDX[dd];
  const int v = 2 * y0 + 1 + (LDY[dd] - LHY[h_in]) + (k - 1) * LDY[dd];
  for (int side = 0; side < 2; ++side) {
    const bool left = (side == 0);
    const int h_out = left ? dd : ((dd + 1) & 3);
    const int su = u + LHX[h_out], sv = v + LHY[h_out];
    if ((su & 1) && (sv & 1)) {
      exit_left = left;
      ex = (su - 1) >> 1;
      ey = (sv - 1) >> 1;
      h_out_o = h_out;
      return true;
    }
  }
  return false;
}

struct MoveSpec { float speed, offset, t; };

static MoveSpec spec_of(Move m, const Robot &rob, const DiagTurns &d) {
  switch (m) {
    case MV_ARC_L: case MV_ARC_R:
      return { rob.arc90_speed, rob.arc90_offset,
               timing::turn_time(90.0f, rob.arc90_omega, rob.arc90_alpha) };
    case MV_ARC_180:
      return { rob.arc180_speed, rob.arc180_offset,
               timing::turn_time(180.0f, rob.arc180_omega, rob.arc180_alpha) };
    case MV_SPIN_L: case MV_SPIN_R:
      return { 0.0f, 0.0f, timing::spin_time(90.0f, rob.spin_omega, rob.spin_alpha) };
    case MV_SPIN_180:
      return { 0.0f, 0.0f, timing::spin_time(180.0f, rob.spin_omega, rob.spin_alpha) };
    case MV_SD45_L: case MV_SD45_R:
      return { d.sd45_speed, d.sd45_offset,
               timing::turn_time(45.0f, d.sd45_omega, d.sd45_alpha) };
    case MV_DS45_L: case MV_DS45_R:
      return { d.ds45_speed, d.ds45_offset,
               timing::turn_time(45.0f, d.ds45_omega, d.ds45_alpha) };
    case MV_DD90_L: case MV_DD90_R:
      return { d.diag_v_max, 63.0f, timing::turn_time(90.0f, 273.0f, 2500.0f) };
    default:
      return { 0.0f, 0.0f, 0.0f };
  }
}

/// One run-then-turn, given the speed and offset left behind by the previous
/// turn. Advances v and off. INF_TIME if the geometry does not fit.
static float step_time(const Step &s, const Robot &rob, const DiagTurns &d,
                       float v_goal, float &v, float &off) {
  timing::MotionModel mm = rob.straight;
  if (s.diag) mm.v_max = d.diag_v_max;
  const float pitch = s.diag ? DIAG_PITCH : 180.0f;

  if (s.move == MV_GOAL) {
    return timing::straight_time(mm, s.cells * pitch - off, v, v_goal);
  }
  const MoveSpec ms = spec_of(s.move, rob, d);
  const float t = timing::straight_time(mm, s.cells * pitch - off - ms.offset, v, ms.speed);
  if (t >= timing::INF_TIME) return timing::INF_TIME;
  v = ms.speed;
  off = ms.offset;
  return t + ms.t;
}

static float v_start_of(const Robot &rob) {
  return timing::fminf_(rob.straight.v_max,
                        sqrtf(2.0f * rob.straight.accel * rob.start_run_up));
}
static float v_goal_of(const Robot &rob) {
  return timing::fminf_(rob.straight.v_max,
                        sqrtf(2.0f * rob.straight.decel * rob.goal_runout));
}

float route_time(const Route &r, const Robot &rob, const DiagTurns &d, Head) {
  if (!r.ok) return 0.0f;
  const float vg = v_goal_of(rob);
  float total = 0.0f, v = v_start_of(rob), off = 0.0f;
  for (int i = 0; i < r.count; ++i) {
    const float t = step_time(r.steps[i], rob, d, vg, v, off);
    if (t >= timing::INF_TIME) return timing::INF_TIME;
    total += t;
    if (r.steps[i].move == MV_GOAL) break;
  }
  return total;
}

void route_times(const Route &r, const Robot &rob, const DiagTurns &d, Head,
                 RouteStepFn fn, void *ctx) {
  if (!r.ok || !fn) return;
  const float vg = v_goal_of(rob);
  float v = v_start_of(rob), off = 0.0f;
  for (int i = 0; i < r.count; ++i) {
    const Step &s = r.steps[i];
    // Recomputed rather than inferred: step_time() advances v and off as a
    // side effect, so the turn's own cost has to be read from the same spec it
    // uses or the split would not add up to the whole.
    const float turn = (s.move == MV_GOAL) ? 0.0f : spec_of(s.move, rob, d).t;
    const float t = step_time(s, rob, d, vg, v, off);
    if (t >= timing::INF_TIME) return;
    fn(ctx, i, s, t - turn, turn);
    if (s.move == MV_GOAL) break;
  }
}

/// Cost steps [i0, i1) exactly as they stand.
static float cost_as_is(const Route &r, int i0, int i1, const Robot &rob,
                        const DiagTurns &d, float v, float off) {
  const float vg = v_goal_of(rob);
  float t = 0.0f;
  for (int i = i0; i < i1 && i < r.count; ++i) {
    const float s = step_time(r.steps[i], rob, d, vg, v, off);
    if (s >= timing::INF_TIME) return timing::INF_TIME;
    t += s;
  }
  return t;
}

/// Cost the same span as SD45 -> diagonal -> DS45, INCLUDING the step that
/// follows it.
///
/// Costing only the section is what made the first version of this produce
/// routes it then had to report as infinite: coming off a diagonal consumes
/// 120 mm of the next cell, so if the following turn is close, the join does
/// not fit. The join has to be inside the comparison, not checked afterwards.
static float cost_as_diagonal(const Route &r, int i0, int k, const Robot &rob,
                              const DiagTurns &d, float v, float off, int next_adj) {
  const float vg = v_goal_of(rob);
  Step sd = {}, ds = {};
  sd.move  = is_left90(r.steps[i0].move) ? MV_SD45_L : MV_SD45_R;
  sd.cells = r.steps[i0].cells;
  ds.move  = MV_DS45_L;   // hand does not change the cost, only the geometry
  ds.cells = (uint8_t)(k - 1);
  ds.diag  = 1;

  float t = step_time(sd, rob, d, vg, v, off);
  if (t >= timing::INF_TIME) return timing::INF_TIME;
  const float t2 = step_time(ds, rob, d, vg, v, off);
  if (t2 >= timing::INF_TIME) return timing::INF_TIME;
  t += t2;

  if (i0 + k < r.count) {
    // The join, costed with the CORRECTED length. Costing it with the original
    // is how the substitution came out both valid and undrivable: the diagonal
    // ends a cell further on, so the straight after it is a cell shorter, and
    // what looked like room for the exit offset plus the next turn's entry is
    // not there.
    Step nxt = r.steps[i0 + k];
    if ((int)nxt.cells - next_adj < 1) return timing::INF_TIME;
    nxt.cells = (uint8_t)((int)nxt.cells - next_adj);
    const float t3 = step_time(nxt, rob, d, vg, v, off);
    if (t3 >= timing::INF_TIME) return timing::INF_TIME;
    t += t3;
  }
  return t;
}

int diagonalise(Route &r, const Robot &rob, const DiagTurns &d, Head start_head) {
  if (!d.enabled || !r.ok || r.count < 3) return 0;

  const float vg = v_goal_of(rob);
  int subs = 0;
  int out = 0;                     // write cursor; the rewrite is never longer
  int i = 0;
  float v = v_start_of(rob);
  float off = 0.0f;

  while (i < r.count) {
    // How far does an alternation of 90s run from here?
    int k = 0;
    if (is_turn90(r.steps[i].move)) {
      k = 1;
      while (i + k < r.count) {
        const Step &a = r.steps[i + k - 1];
        const Step &b = r.steps[i + k];
        if (!is_turn90(b.move)) break;
        if (b.cells != 1) break;                             // turns not adjacent
        if (is_left90(a.move) == is_left90(b.move)) break;    // same hand: not a zigzag
        ++k;
      }
    }

    // Three turns is the shortest zigzag worth costing: two would leave a
    // diagonal of one step, which the two 45s consume entirely.
    bool exit_left = false;
    int  ex = 0, ey = 0, h_out = 0, next_adj = 0;
    if (k >= 3) {
      const int h_in = (out == 0) ? (int)start_head : (int)r.steps[out - 1].heading;
      if (!diag_exit(r.steps[i].x, r.steps[i].y, h_in, is_left90(r.steps[i].move),
                     k, exit_left, ex, ey, h_out)) {
        k = 0;                  // no legal way off the diagonal
      } else if (i + k < r.count) {
        // The step after the zigzag counted its cells from where the LAST TURN
        // was. The diagonal comes off somewhere else, so that count has to move
        // by the difference along the direction it travels.
        const int zx = r.steps[i + k - 1].x, zy = r.steps[i + k - 1].y;
        next_adj = (ex - zx) * LHX[h_out] + (ey - zy) * LHY[h_out];
        if ((int)r.steps[i + k].cells - next_adj < 1) k = 0;   // no room left
        // Anything off-axis means the diagonal did not land on the same line
        // the following straight runs along, and no adjustment can fix that.
        if ((ex - zx) - next_adj * LHX[h_out] != 0) k = 0;
        if ((ey - zy) - next_adj * LHY[h_out] != 0) k = 0;
      } else {
        k = 0;    // substitution running into the goal: not handled, left alone
      }
    }
    if (k >= 3) {
      const int join = (i + k < r.count) ? 1 : 0;
      const float t_ortho = cost_as_is(r, i, i + k + join, rob, d, v, off);
      if (t_ortho >= timing::INF_TIME) { /* fall through: diagonal may still fit */ }
      const float t_diag  = cost_as_diagonal(r, i, k, rob, d, v, off, next_adj);

      if (t_diag < t_ortho) {
        const Step entry = r.steps[i];
        const Step exitp = r.steps[i + k - 1];
        const bool first_left = is_left90(entry.move);
        const bool last_left  = exit_left;   // what the lattice allows, not what the zigzag did

        r.steps[out] = entry;
        r.steps[out].move  = first_left ? MV_SD45_L : MV_SD45_R;
        r.steps[out].diag  = 0;
        r.steps[out].t     = 0.0f;
        ++out;

        r.steps[out] = exitp;
        r.steps[out].move  = last_left ? MV_DS45_L : MV_DS45_R;
        r.steps[out].cells = (uint8_t)(k - 1);
        r.steps[out].diag  = 1;
        r.steps[out].t     = 0.0f;
        ++out;

        r.steps[out - 1].x = (uint8_t)ex;      // where the diagonal really ends
        r.steps[out - 1].y = (uint8_t)ey;
        r.steps[out - 1].heading = (Head)h_out;
        r.steps[i + k].cells = (uint8_t)((int)r.steps[i + k].cells - next_adj);

        v   = d.ds45_speed;       // advance past the two new steps
        off = d.ds45_offset;
        i  += k;
        ++subs;
        continue;
      }
    }

    // No substitution: copy through, keeping v and off in step.
    const Step s = r.steps[i];
    r.steps[out] = s;
    step_time(s, rob, d, vg, v, off);
    ++out;
    ++i;
  }

  r.count = out;
  return subs;
}

}  // namespace plan
