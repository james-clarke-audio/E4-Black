#include "planner.h"
#include <math.h>

namespace plan {

static const int DX[4] = {0, 1, 0, -1};   // N, E, S, W
static const int DY[4] = {1, 0, -1, 0};

static inline int node_id(int x, int y, int h, int s) {
  return ((y * W + x) * NHEAD + h) * NSPEED + s;
}

// ---------------------------------------------------------------------------
// COST SCALE
//
// One integer key serves both objectives, chosen so a single edge never exceeds
// BUCKETS-1. That bound is what lets the queue be a small circular array
// instead of one bucket per reachable cost.
//
//   QUICKEST : centiseconds. 10 ms on a 30 s run is 0.03% -- far below the
//              accuracy of the turn table it is fed from.
//   SHORTEST : cells * 256 + min(centiseconds, 255). Lexicographic: 256 > 255,
//              so one more cell always outweighs the entire time field, and
//              time only separates routes of equal length. Without that
//              tie-break "shortest" returns an arbitrary one of the many
//              equally-short routes and reports a meaningless time for it.
//
// Worst single edge: 15 cells * 256 + 255 = 4095.
// ---------------------------------------------------------------------------
static const int BUCKETS  = 4096;
static const uint32_t COST_INF = 0xFFFFFFFFu;

// ---------------------------------------------------------------------------
// Dial's algorithm -- a circular bucket queue.
//
// The old flood used a plain FIFO, correct ONLY while every edge costs the
// same. That is what unit cost bought and why the Arduino could get away with
// it; with real times the edges differ and a FIFO gives wrong answers. A binary
// heap would do it in O(E log V). This does it in O(E) because the weights are
// small bounded integers, and because keys only ever move forwards the buckets
// can be reused round a ring of BUCKETS entries rather than one per cost.
//
// Storage: bucket heads plus one "next" link per node. Intrusive, so no
// allocation and no per-push bookkeeping.
// ---------------------------------------------------------------------------
// The lists are DOUBLY linked. Singly linked is the obvious choice and it is
// wrong: improving a node's cost means moving it to a different bucket, and a
// singly linked node cannot be unlinked from the one it is already in. Writing
// its `next` to the new bucket's head orphans everything chained behind it in
// the old one -- the search then quietly loses nodes and reports no route for a
// maze that plainly has one. Costs one extra index per node to fix.
static int16_t  s_bucket[BUCKETS];
static int16_t  s_next[NNODES];
static int16_t  s_prev[NNODES];
static int16_t  s_inb[NNODES];      // which bucket this node is in, -1 = none
static uint32_t s_dist[NNODES];
static int16_t  s_parent[NNODES];
static uint8_t  s_pmove[NNODES];
static uint8_t  s_pcells[NNODES];
static uint8_t  s_done[(NNODES + 7) / 8];
// 36.3 KB of .bss, measured on the target, not estimated:
//   s_bucket 8192  s_dist 8192  s_next/prev/inb/parent 4096 each
//   s_pmove/s_pcells 2048 each  s_done 256
// plus 3.7 KB of .text. That is 28% of the F411's RAM -- but each array gets
// its own section under -fdata-sections, so --gc-sections drops the lot if
// nothing calls plan_route. It costs nothing until it is used.

static inline bool is_done(int n) { return (s_done[n >> 3] >> (n & 7)) & 1; }
static inline void set_done(int n) { s_done[n >> 3] |= (uint8_t)(1u << (n & 7)); }

static inline void q_unlink(int m) {
  const int b = s_inb[m];
  if (b < 0) return;
  if (s_prev[m] >= 0) s_next[s_prev[m]] = s_next[m];
  else                s_bucket[b]       = s_next[m];
  if (s_next[m] >= 0) s_prev[s_next[m]] = s_prev[m];
  s_inb[m] = -1; s_next[m] = -1; s_prev[m] = -1;
}

static inline void q_push(int m, int b) {
  q_unlink(m);
  s_prev[m] = -1;
  s_next[m] = s_bucket[b];
  if (s_bucket[b] >= 0) s_prev[s_bucket[b]] = (int16_t)m;
  s_bucket[b] = (int16_t)m;
  s_inb[m] = (int16_t)b;
}

struct TurnSpec {
  float   t;        // seconds for the turn itself
  float   speed;    // constant forward speed through it (0 = spin in place)
  float   offset;   // path consumed either side of the cell centre
  Move    move;
};

/// The ways a given change of heading can actually be executed.
static int turn_options(const Robot &r, int from, int to, TurnSpec out[2]) {
  int n = 0;
  const bool is_left  = (to == ((from + 3) & 3));
  const bool is_right = (to == ((from + 1) & 3));
  const bool is_back  = (to == ((from + 2) & 3));

  if (is_left || is_right) {
    out[n].t = timing::turn_time(90.0f, r.arc90_omega, r.arc90_alpha);
    out[n].speed = r.arc90_speed; out[n].offset = r.arc90_offset;
    out[n].move = is_left ? MV_ARC_L : MV_ARC_R; ++n;
    out[n].t = timing::spin_time(90.0f, r.spin_omega, r.spin_alpha);
    out[n].speed = 0.0f; out[n].offset = 0.0f;
    out[n].move = is_left ? MV_SPIN_L : MV_SPIN_R; ++n;
  } else if (is_back) {
    if (r.allow_arc180) {
      out[n].t = timing::turn_time(180.0f, r.arc180_omega, r.arc180_alpha);
      out[n].speed = r.arc180_speed; out[n].offset = r.arc180_offset;
      out[n].move = MV_ARC_180; ++n;
    }
    out[n].t = timing::spin_time(180.0f, r.spin_omega, r.spin_alpha);
    out[n].speed = 0.0f; out[n].offset = 0.0f;
    out[n].move = MV_SPIN_180; ++n;
  }
  return n;
}

static inline float speed_of_class(const Robot &r, int s)  { return s ? r.arc90_speed : 0.0f; }
static inline float offset_of_class(const Robot &r, int s) { return s ? r.arc90_offset : 0.0f; }

/// True if any edge exceeded the ring size on the last plan. Dial's algorithm
/// is only correct while every edge is smaller than the ring, and that bound
/// depends on the Robot it is handed -- a very low v_max makes a 15-cell
/// straight take longer than the ring can express. Clamping would silently
/// return a wrong route, so instead the plan is marked invalid and says so.
static bool s_key_overflow = false;

static inline int edge_key(Objective obj, float t, int cells) {
  int cs = (int)lroundf(t * 100.0f);
  if (cs < 0) cs = 0;
  int k;
  if (obj == QUICKEST) {
    k = cs;                                   // centiseconds
  } else {
    k = cells * 256 + (cs > 255 ? 255 : cs);  // cells first, time as tie-break
  }
  if (k >= BUCKETS) { s_key_overflow = true; k = BUCKETS - 1; }
  return k;
}

void plan_route(Route &route, const WallReader &maze, const Robot &r, Objective obj,
                int sx, int sy, Head start_head,
                int gx, int gy, int gw, int gh) {
  route = Route();

  for (int i = 0; i < NNODES; ++i) {
    s_dist[i] = COST_INF; s_parent[i] = -1;
    s_next[i] = -1; s_prev[i] = -1; s_inb[i] = -1;
  }
  for (int i = 0; i < BUCKETS; ++i) s_bucket[i] = -1;
  s_key_overflow = false;
  for (unsigned i = 0; i < sizeof(s_done); ++i) s_done[i] = 0;

  const int start_id = node_id(sx, sy, start_head, 0);
  s_dist[start_id] = 0;
  q_push(start_id, 0);

  // Free run-up inside the start square; and the entry speed the goal run-out allows.
  const float v_start = timing::fminf_(r.straight.v_max,
                                       sqrtf(2.0f * r.straight.accel * r.start_run_up));
  const float v_goal  = timing::fminf_(r.straight.v_max,
                                       sqrtf(2.0f * r.straight.decel * r.goal_runout));

  int      best_goal_node  = -1;
  uint32_t best_goal_cost  = COST_INF;
  int      best_goal_cells = 0;
  float    best_goal_t     = 0.0f;

  uint32_t key = 0;
  int      scanned = 0;

  // Sweep the ring. `scanned` counts consecutive empty buckets; once a whole
  // ring has gone by with nothing in it, every remaining node is unreachable.
  while (scanned <= BUCKETS) {
    const int b = (int)(key % BUCKETS);
    if (s_bucket[b] < 0) { ++key; ++scanned; continue; }
    scanned = 0;

    const int id = s_bucket[b];
    q_unlink(id);

    if (is_done(id)) continue;
    set_done(id);
    if (key >= best_goal_cost) break;       // nothing cheaper can remain

    const int s = id % NSPEED;
    const int h = (id / NSPEED) % NHEAD;
    const int c = id / (NSPEED * NHEAD);
    const int x = c % W, y = c / W;

    const bool  at_start = (id == start_id);
    const float v_out    = at_start ? v_start : speed_of_class(r, s);
    const float off_out  = at_start ? 0.0f    : offset_of_class(r, s);

    int cx = x, cy = y;
    for (int n = 1; n <= W + H; ++n) {
      if (!maze.is_exit(maze.ctx, cx, cy, h)) break;
      cx += DX[h];
      cy += DY[h];
      if (cx < 0 || cx >= W || cy < 0 || cy >= H) break;

      // --- goal reached ----------------------------------------------------
      if (cx >= gx && cx < gx + gw && cy >= gy && cy < gy + gh) {
        const float d = n * 180.0f - off_out;
        const float t = timing::straight_time(r.straight, d, v_out, v_goal);
        if (t < timing::INF_TIME) {
          const uint32_t cost = key + (uint32_t)edge_key(obj, t, n);
          if (cost < best_goal_cost) {
            best_goal_cost  = cost;
            best_goal_node  = id;
            best_goal_cells = n;
            best_goal_t     = t;
          }
        }
      }

      // --- turn here -------------------------------------------------------
      for (int dir = 0; dir < 3; ++dir) {
        const int h2 = (dir == 0) ? ((h + 3) & 3) : (dir == 1) ? ((h + 1) & 3) : ((h + 2) & 3);
        TurnSpec opt[2];
        const int nopt = turn_options(r, h, h2, opt);
        for (int k = 0; k < nopt; ++k) {
          const TurnSpec &o = opt[k];
          // A smooth arc has to leave the cell it turns in, so that exit must be
          // open. A spin turns on the spot and does not care.
          if (o.speed > 0.0f && !maze.is_exit(maze.ctx, cx, cy, h2)) continue;

          const float d = n * 180.0f - off_out - o.offset;
          float t = timing::straight_time(r.straight, d, v_out, o.speed);
          if (t >= timing::INF_TIME) continue;   // no room to reach that entry speed
          t += o.t;

          const int m = node_id(cx, cy, h2, (o.speed > 0.0f) ? 1 : 0);
          const uint32_t cost = key + (uint32_t)edge_key(obj, t, n);
          if (cost < s_dist[m]) {
            s_dist[m]   = cost;
            s_parent[m] = (int16_t)id;
            s_pmove[m]  = (uint8_t)o.move;
            s_pcells[m] = (uint8_t)n;
            q_push(m, (int)(cost % BUCKETS));
          }
        }
      }
    }
  }

  if (best_goal_node < 0) return;
  if (s_key_overflow) { route.overflow = true; return; }

  // --- walk the parents back straight into route.steps, then reverse in place
  // No scratch array: a second Step[MAX_STEPS] would be another 1 KB of stack,
  // which a Cortex-M does not have going spare.
  Step *st = route.steps;
  int nt = 0;
  st[nt].move = MV_GOAL; st[nt].cells = (uint8_t)best_goal_cells;
  st[nt].x = (uint8_t)gx; st[nt].y = (uint8_t)gy;
  st[nt].heading = NN;    st[nt].t = best_goal_t; ++nt;

  int cur = best_goal_node;
  while (cur != start_id && cur >= 0 && nt < MAX_STEPS) {
    const int h = (cur / NSPEED) % NHEAD;
    const int c = cur / (NSPEED * NHEAD);
    st[nt].move    = (Move)s_pmove[cur];
    st[nt].cells   = s_pcells[cur];
    st[nt].x       = (uint8_t)(c % W);
    st[nt].y       = (uint8_t)(c / W);
    st[nt].heading = (Head)h;
    st[nt].t       = 0.0f;
    ++nt;
    cur = s_parent[cur];
  }
  if (nt >= MAX_STEPS) { route.truncated = true; return; }
  for (int i = 0, j = nt - 1; i < j; ++i, --j) { Step t2 = st[i]; st[i] = st[j]; st[j] = t2; }

  // Per-step time: for QUICKEST the key IS centiseconds, so differences along
  // the chain give each step back exactly. For SHORTEST the key is packed, so
  // only the total is meaningful and per-step is left at zero.
  route.ok = true;
  route.count = nt;
  for (int i = 0; i < nt; ++i) {
    const Step &st = route.steps[i];
    route.cells += st.cells;
    if (st.move == MV_ARC_L || st.move == MV_ARC_R || st.move == MV_ARC_180) ++route.turns;
    if (st.move == MV_SPIN_L || st.move == MV_SPIN_R || st.move == MV_SPIN_180) ++route.spins;
  }
  route.seconds = (obj == QUICKEST)
                    ? (float)best_goal_cost / 100.0f
                    : 0.0f;   // set by the caller's re-cost, or use QUICKEST for time
  if (obj == SHORTEST) {
    // Re-cost the chosen route honestly in seconds: the packed key cannot be
    // read as a time, and reporting it as one would be a lie.
    route.seconds = 0.0f;
    float v = v_start;
    float off = 0.0f;
    for (int i = 0; i < nt; ++i) {
      const Step &st = route.steps[i];
      if (st.move == MV_GOAL) {
        route.seconds += timing::straight_time(r.straight, st.cells * 180.0f - off, v, v_goal);
        break;
      }
      TurnSpec opt[2];
      const int hprev = (i == 0) ? (int)start_head : (int)route.steps[i - 1].heading;
      const int nopt = turn_options(r, hprev, (int)st.heading, opt);
      // pick the implementation the search actually chose
      for (int k = 0; k < nopt; ++k) {
        if (opt[k].move != st.move) continue;
        route.seconds += timing::straight_time(r.straight, st.cells * 180.0f - off - opt[k].offset,
                                               v, opt[k].speed) + opt[k].t;
        v = opt[k].speed;
        off = opt[k].offset;
        break;
      }
    }
  }
}

}  // namespace plan
