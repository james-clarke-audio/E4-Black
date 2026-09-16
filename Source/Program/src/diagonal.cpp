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
                              const DiagTurns &d, float v, float off) {
  const float vg = v_goal_of(rob);
  Step sd = {}, ds = {};
  sd.move  = is_left90(r.steps[i0].move) ? MV_SD45_L : MV_SD45_R;
  sd.cells = r.steps[i0].cells;
  ds.move  = is_left90(r.steps[i0 + k - 1].move) ? MV_DS45_L : MV_DS45_R;
  ds.cells = (uint8_t)(k - 1);
  ds.diag  = 1;

  float t = step_time(sd, rob, d, vg, v, off);
  if (t >= timing::INF_TIME) return timing::INF_TIME;
  const float t2 = step_time(ds, rob, d, vg, v, off);
  if (t2 >= timing::INF_TIME) return timing::INF_TIME;
  t += t2;

  if (i0 + k < r.count) {                       // the join into the next turn
    const float t3 = step_time(r.steps[i0 + k], rob, d, vg, v, off);
    if (t3 >= timing::INF_TIME) return timing::INF_TIME;
    t += t3;
  }
  return t;
}

int diagonalise(Route &r, const Robot &rob, const DiagTurns &d, Head) {
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
    if (k >= 3) {
      const int join = (i + k < r.count) ? 1 : 0;
      const float t_ortho = cost_as_is(r, i, i + k + join, rob, d, v, off);
      const float t_diag  = cost_as_diagonal(r, i, k, rob, d, v, off);

      if (t_diag < t_ortho) {
        const Step entry = r.steps[i];
        const Step exitp = r.steps[i + k - 1];
        const bool first_left = is_left90(entry.move);
        const bool last_left  = is_left90(exitp.move);

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
