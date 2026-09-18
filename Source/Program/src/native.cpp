#include "native.h"
#include <math.h>

namespace plan {

// Orthogonal unit steps, in half-cells, indexed by Head (N, E, S, W).
static const int HX[4] = { 0,  1,  0, -1 };
static const int HY[4] = { 1,  0, -1,  0 };
// Diagonal unit steps, indexed by Diag (NE, SE, SW, NW).
static const int DXd[4] = { 1,  1, -1, -1 };
static const int DYd[4] = { 1, -1, -1,  1 };

// diag_ccw / diag_cw now live in native.h: the route executor needs them too,
// and two copies of that mapping is exactly how one of them went wrong.

// ---------------------------------------------------------------------------
// Wall-midpoint numbering. Vertical walls first, then horizontal.
//   vertical   u even, v odd:   u = 2x, x in 0..16 ; v = 2y+1, y in 0..15
//   horizontal u odd,  v even:  u = 2x+1, x in 0..15 ; v = 2y, y in 0..16
// Returns -1 for anything that is not a wall midpoint (a centre or a post).
// ---------------------------------------------------------------------------
static inline int wall_index(int u, int v) {
  if (u < 0 || v < 0 || u > 2 * W || v > 2 * H) return -1;
  const int ue = !(u & 1), ve = !(v & 1);
  if (ue && !ve) {                      // vertical
    const int x = u >> 1, y = (v - 1) >> 1;
    return x * H + y;
  }
  if (!ue && ve) {                      // horizontal
    const int x = (u - 1) >> 1, y = v >> 1;
    return N_VWALL + x * (H + 1) + y;
  }
  return -1;
}

static inline int centre_id(int x, int y, int h, int s) {
  return ((y * W + x) * NHEAD + h) * NSPEED + s;
}
static inline int diag_id(int u, int v, int dd) {
  const int w = wall_index(u, v);
  return (w < 0) ? -1 : N_CENTRE + w * 4 + dd;
}

/// Is the wall at half-cell point (u,v) an opening of cell (cx,cy)?
/// Works out which side of that cell it is and asks the map.
static bool wall_open_for(const WallReader &m, int cx, int cy, int u, int v) {
  if (!(u & 1)) {                       // vertical wall: u = 2 * boundary x
    const int bx = u >> 1;
    if (bx == cx)     return m.is_exit(m.ctx, cx, cy, WW);
    if (bx == cx + 1) return m.is_exit(m.ctx, cx, cy, EE);
    return false;
  }
  const int by = v >> 1;                // horizontal wall
  if (by == cy)     return m.is_exit(m.ctx, cx, cy, SS);
  if (by == cy + 1) return m.is_exit(m.ctx, cx, cy, NN);
  return false;
}

/// One diagonal step from wall midpoint (u,v) in direction dd. Fills the cell
/// it crosses and the wall it lands on. False if it leaves the maze or either
/// wall is closed.
static bool diag_step(const WallReader &m, int u, int v, int dd,
                      int &u2, int &v2, int &cx, int &cy) {
  u2 = u + DXd[dd];
  v2 = v + DYd[dd];
  if (wall_index(u2, v2) < 0) return false;
  // The crossed cell contains the MIDPOINT of the step, which is at
  // ((u+u2)/2, (v+v2)/2) in half-cells, so the cell is that over two again.
  // u+u2 is always odd (one wall coordinate is even, the other odd), so the
  // shift floors correctly for both directions of travel.
  cx = (u + u2) >> 2;
  cy = (v + v2) >> 2;
  if (cx < 0 || cx >= W || cy < 0 || cy >= H) return false;
  if (!wall_open_for(m, cx, cy, u, v))   return false;
  if (!wall_open_for(m, cx, cy, u2, v2)) return false;
  return true;
}

// ---------------------------------------------------------------------------
// Queue: same Dial ring as planner.cpp, sized for this lattice. Doubly linked
// for the same reason -- improving a node means moving it between buckets, and
// a singly linked node cannot be unlinked from the one it is in.
// ---------------------------------------------------------------------------
static const int BUCKETS_N = 4096;
static const uint16_t DINF = 0xFFFFu;

static int16_t  q_bucket[BUCKETS_N];
static int16_t  q_next[N_NATIVE];
static int16_t  q_prev[N_NATIVE];
static uint16_t n_dist[N_NATIVE];
static int16_t  n_parent[N_NATIVE];
static uint8_t  n_pmove[N_NATIVE];
static uint8_t  n_pcells[N_NATIVE];
static uint8_t  n_done[(N_NATIVE + 7) / 8];
static bool     n_overflow;
// ~46 KB of .bss. It supersedes planner.cpp's 36 KB rather than adding to it
// once something drives this; both are dropped by --gc-sections until then.

static inline bool ndone(int n)  { return (n_done[n >> 3] >> (n & 7)) & 1; }
static inline void nset(int n)   { n_done[n >> 3] |= (uint8_t)(1u << (n & 7)); }

static inline void qunlink(int m) {
  const int b = n_dist[m] % BUCKETS_N;
  if (q_prev[m] >= 0)      q_next[q_prev[m]] = q_next[m];
  else if (q_bucket[b] == m) q_bucket[b] = q_next[m];
  if (q_next[m] >= 0)      q_prev[q_next[m]] = q_prev[m];
  q_next[m] = -1; q_prev[m] = -1;
}
static inline void qpush(int m, int cost) {
  if (n_dist[m] != DINF) qunlink(m);
  n_dist[m] = (uint16_t)cost;
  const int b = cost % BUCKETS_N;
  q_prev[m] = -1;
  q_next[m] = q_bucket[b];
  if (q_bucket[b] >= 0) q_prev[q_bucket[b]] = (int16_t)m;
  q_bucket[b] = (int16_t)m;
}

static inline int key_of(Objective obj, float t, int cells) {
  int cs = (int)lroundf(t * 100.0f);
  if (cs < 0) cs = 0;
  int k = (obj == QUICKEST) ? cs : cells * 256 + (cs > 255 ? 255 : cs);
  if (k >= BUCKETS_N) { n_overflow = true; k = BUCKETS_N - 1; }
  return k;
}

struct Spec { float speed, offset, t; Move move; };

static Spec spec_arc90(const Robot &r, bool left) {
  return { r.arc90_speed, r.arc90_offset,
           timing::turn_time(90.0f, r.arc90_omega, r.arc90_alpha),
           left ? MV_ARC_L : MV_ARC_R };
}
static Spec spec_spin90(const Robot &r, bool left) {
  return { 0.0f, 0.0f, timing::spin_time(90.0f, r.spin_omega, r.spin_alpha),
           left ? MV_SPIN_L : MV_SPIN_R };
}
static Spec spec_spin180(const Robot &r) {
  return { 0.0f, 0.0f, timing::spin_time(180.0f, r.spin_omega, r.spin_alpha), MV_SPIN_180 };
}
static Spec spec_arc180(const Robot &r) {
  return { r.arc180_speed, r.arc180_offset,
           timing::turn_time(180.0f, r.arc180_omega, r.arc180_alpha), MV_ARC_180 };
}
static Spec spec_sd45(const DiagTurns &d, bool left) {
  return { d.sd45_speed, d.sd45_offset,
           timing::turn_time(45.0f, d.sd45_omega, d.sd45_alpha),
           left ? MV_SD45_L : MV_SD45_R };
}
static Spec spec_ds45(const DiagTurns &d, bool left) {
  return { d.ds45_speed, d.ds45_offset,
           timing::turn_time(45.0f, d.ds45_omega, d.ds45_alpha),
           left ? MV_DS45_L : MV_DS45_R };
}
static Spec spec_dd90(const DiagTurns &d, bool left) {
  return { d.diag_v_max, 63.0f, timing::turn_time(90.0f, 273.0f, 2500.0f),
           left ? MV_DD90_L : MV_DD90_R };
}

static inline float v_start_of(const Robot &r) {
  return timing::fminf_(r.straight.v_max, sqrtf(2.0f * r.straight.accel * r.start_run_up));
}
static inline float v_goal_of(const Robot &r) {
  return timing::fminf_(r.straight.v_max, sqrtf(2.0f * r.straight.decel * r.goal_runout));
}

void plan_native(Route &out, const WallReader &maze, const Robot &r,
                 const DiagTurns &d, Objective obj,
                 int sx, int sy, Head start_head,
                 int gx, int gy, int gw, int gh) {
  out = Route();
  for (int i = 0; i < N_NATIVE; ++i) {
    n_dist[i] = DINF; n_parent[i] = -1; q_next[i] = -1; q_prev[i] = -1;
  }
  for (int i = 0; i < BUCKETS_N; ++i) q_bucket[i] = -1;
  for (unsigned i = 0; i < sizeof(n_done); ++i) n_done[i] = 0;
  n_overflow = false;

  const float vs = v_start_of(r), vg = v_goal_of(r);
  const int start_id = centre_id(sx, sy, start_head, 0);
  qpush(start_id, 0);

  int best_node = -1, best_cells = 0;
  uint32_t best_cost = 0xFFFFFFFFu;
  float best_t = 0.0f;

  uint32_t key = 0;
  int scanned = 0;

  while (scanned <= BUCKETS_N) {
    const int b = (int)(key % BUCKETS_N);
    if (q_bucket[b] < 0) { ++key; ++scanned; continue; }
    scanned = 0;
    const int id = q_bucket[b];
    if (n_dist[id] != key) { qunlink(id); continue; }   // ring collision with a later key
    qunlink(id);
    if (ndone(id)) continue;
    nset(id);
    if (key >= best_cost) break;

    if (id < N_CENTRE) {
      // ---- at a cell centre, heading orthogonally -------------------------
      const int s = id % NSPEED;
      const int h = (id / NSPEED) % NHEAD;
      const int c = id / (NSPEED * NHEAD);
      const int x = c % W, y = c / W;
      const bool at_start = (id == start_id);
      const float v_out   = at_start ? vs : (s ? r.arc90_speed : 0.0f);
      const float off_out = at_start ? 0.0f : (s ? r.arc90_offset : 0.0f);

      // SHE MAY ALREADY BE THERE. Every other goal test in this function sits
      // inside the loop below, which advances a cell before it looks -- so a
      // route that ARRIVES at the goal by turning into it was not recognised
      // as finished, and had to drive at least one more cell before the
      // planner would call it done. On a route ending in a DS45 that is a
      // whole cell given away after the clock has already stopped.
      //
      // Nothing more to do costs nothing more: the turn that brought her here
      // is already paid for in `key`.
      if (x >= gx && x < gx + gw && y >= gy && y < gy + gh) {
        if (key < best_cost) {
          best_cost = key; best_node = id; best_cells = 0; best_t = 0.0f;
        }
      }

      int cx = x, cy = y;
      for (int n = 1; n <= W + H; ++n) {
        if (!maze.is_exit(maze.ctx, cx, cy, h)) break;
        cx += HX[h]; cy += HY[h];
        if (cx < 0 || cx >= W || cy < 0 || cy >= H) break;

        if (cx >= gx && cx < gx + gw && cy >= gy && cy < gy + gh) {
          const float t = timing::straight_time(r.straight, n * 180.0f - off_out, v_out, vg);
          if (t < timing::INF_TIME) {
            const uint32_t cost = key + (uint32_t)key_of(obj, t, n);
            if (cost < best_cost) {
              best_cost = cost; best_node = id; best_cells = n; best_t = t;
            }
          }
        }

        // 90s and 180s, arc or spin, staying orthogonal.
        for (int dir = 0; dir < 3; ++dir) {
          const int h2 = (dir == 0) ? ((h + 3) & 3) : (dir == 1) ? ((h + 1) & 3) : ((h + 2) & 3);
          Spec opt[2]; int nopt = 0;
          if (dir < 2) { opt[nopt++] = spec_arc90(r, dir == 0); opt[nopt++] = spec_spin90(r, dir == 0); }
          else         { if (r.allow_arc180) opt[nopt++] = spec_arc180(r); opt[nopt++] = spec_spin180(r); }
          for (int k = 0; k < nopt; ++k) {
            const Spec &o = opt[k];
            if (o.speed > 0.0f && !maze.is_exit(maze.ctx, cx, cy, h2)) continue;
            float t = timing::straight_time(r.straight, n * 180.0f - off_out - o.offset, v_out, o.speed);
            if (t >= timing::INF_TIME) continue;
            t += o.t;
            const int m = centre_id(cx, cy, h2, (o.speed > 0.0f) ? 1 : 0);
            const uint32_t cost = key + (uint32_t)key_of(obj, t, n);
            if (cost < n_dist[m]) {
              n_parent[m] = (int16_t)id; n_pmove[m] = (uint8_t)o.move; n_pcells[m] = (uint8_t)n;
              qpush(m, (int)cost);
            }
          }
        }

        // SD45: leave the centreline for a diagonal. The landing wall midpoint
        // is the cell's wall in direction (d - h), which is the orthogonal step
        // 45 degrees from h towards d.
        if (d.enabled) {
          for (int side = 0; side < 2; ++side) {
            const bool left = (side == 0);
            const Diag dd = left ? Diag((h + 3) & 3) : Diag(h);
            const int wu = (2 * cx + 1) + (DXd[dd] - HX[h]);
            const int wv = (2 * cy + 1) + (DYd[dd] - HY[h]);
            const int m = diag_id(wu, wv, dd);
            if (m < 0) continue;
            if (!wall_open_for(maze, cx, cy, wu, wv)) continue;
            const Spec o = spec_sd45(d, left);
            float t = timing::straight_time(r.straight, n * 180.0f - off_out - o.offset, v_out, o.speed);
            if (t >= timing::INF_TIME) continue;
            t += o.t;
            const uint32_t cost = key + (uint32_t)key_of(obj, t, n);
            if (cost < n_dist[m]) {
              n_parent[m] = (int16_t)id; n_pmove[m] = (uint8_t)o.move; n_pcells[m] = (uint8_t)n;
              qpush(m, (int)cost);
            }
          }
        }
      }
    } else {
      // ---- on a diagonal, at a wall midpoint ------------------------------
      const int rel = id - N_CENTRE;
      const int w = rel / 4;
      const int dd = rel % 4;
      int u, v;
      if (w < N_VWALL) { u = 2 * (w / H); v = 2 * (w % H) + 1; }
      else             { const int q = w - N_VWALL; u = 2 * (q / (H + 1)) + 1; v = 2 * (q % (H + 1)); }

      timing::MotionModel dm = r.straight;
      dm.v_max = d.diag_v_max;

      int cu = u, cv = v;
      for (int n = 1; n <= 2 * (W + H); ++n) {
        int nu, nv, ccx, ccy;
        if (!diag_step(maze, cu, cv, dd, nu, nv, ccx, ccy)) break;
        cu = nu; cv = nv;

        // DS45 back onto a centreline.
        //
        // Only ONE of the two 45s is ever legal, and which one depends on the
        // wall she is standing on. At a vertical-wall midpoint she is on a row
        // centreline but straddling a wall line, so she can only turn onto E or
        // W; turning onto N would leave her running along the wall. At a
        // horizontal-wall midpoint it is the other way about. The parity test
        // below is exactly that rule: the landing point (u,v) + h has to be a
        // cell centre, and for the illegal turn it lands on a post.
        for (int side = 0; side < 2; ++side) {
          const bool left = (side == 0);
          const Head h2 = left ? diag_ccw(Diag(dd)) : diag_cw(Diag(dd));
          const int su = cu + HX[h2];
          const int sv = cv + HY[h2];
          if ((su & 1) == 0 || (sv & 1) == 0) continue;      // lands on a post: not a turn she can make
          const int ex = (su - 1) >> 1, ey = (sv - 1) >> 1;
          if (ex < 0 || ex >= W || ey < 0 || ey >= H) continue;
          if (!wall_open_for(maze, ex, ey, cu, cv)) continue;
          const Spec o = spec_ds45(d, left);
          float t = timing::straight_time(dm, n * DIAG_PITCH - d.sd45_offset - o.offset,
                                          d.sd45_speed, o.speed);
          if (t >= timing::INF_TIME) continue;
          t += o.t;
          const int m = centre_id(ex, ey, h2, 1);
          const uint32_t cost = key + (uint32_t)key_of(obj, t, n);
          if (cost < n_dist[m]) {
            n_parent[m] = (int16_t)id; n_pmove[m] = (uint8_t)o.move; n_pcells[m] = (uint8_t)n;
            qpush(m, (int)cost);
          }
        }

        // DD90: turn the corner without leaving the diagonal.
        for (int side = 0; side < 2; ++side) {
          const bool left = (side == 0);
          const int dd2 = left ? ((dd + 3) & 3) : ((dd + 1) & 3);
          const int m = diag_id(cu, cv, dd2);
          if (m < 0) continue;
          int tu, tv, tcx, tcy;
          if (!diag_step(maze, cu, cv, dd2, tu, tv, tcx, tcy)) continue;   // must be able to leave
          const Spec o = spec_dd90(d, left);
          float t = timing::straight_time(dm, n * DIAG_PITCH - d.sd45_offset - o.offset,
                                          d.sd45_speed, o.speed);
          if (t >= timing::INF_TIME) continue;
          t += o.t;
          const uint32_t cost = key + (uint32_t)key_of(obj, t, n);
          if (cost < n_dist[m]) {
            n_parent[m] = (int16_t)id; n_pmove[m] = (uint8_t)o.move; n_pcells[m] = (uint8_t)n;
            qpush(m, (int)cost);
          }
        }
      }
    }
  }

  if (best_node < 0) return;
  if (n_overflow) { out.overflow = true; return; }

  Step *st = out.steps;
  int nt = 0;
  st[nt].move = MV_GOAL; st[nt].cells = (uint8_t)best_cells; st[nt].diag = 0;
  st[nt].x = (uint8_t)gx; st[nt].y = (uint8_t)gy; st[nt].heading = NN; st[nt].t = best_t;
  ++nt;

  int cur = best_node;
  while (cur != start_id && cur >= 0 && nt < MAX_STEPS) {
    const Move mv = (Move)n_pmove[cur];
    const bool from_diag = (mv == MV_DS45_L || mv == MV_DS45_R ||
                            mv == MV_DD90_L || mv == MV_DD90_R);
    st[nt].move  = mv;
    st[nt].cells = n_pcells[cur];
    st[nt].diag  = from_diag ? 1 : 0;
    if (cur < N_CENTRE) {
      const int c = cur / (NSPEED * NHEAD);
      st[nt].x = (uint8_t)(c % W); st[nt].y = (uint8_t)(c / W);
      st[nt].heading = Head((cur / NSPEED) % NHEAD);
    } else {
      st[nt].x = 0; st[nt].y = 0; st[nt].heading = NN;   // on a wall, not a cell
    }
    st[nt].t = 0.0f;
    ++nt;
    cur = n_parent[cur];
  }
  if (nt >= MAX_STEPS) { out.truncated = true; return; }
  for (int i = 0, j = nt - 1; i < j; ++i, --j) { Step t2 = st[i]; st[i] = st[j]; st[j] = t2; }

  out.ok = true;
  out.count = nt;
  for (int i = 0; i < nt; ++i) {
    const Step &s = out.steps[i];
    out.cells += s.cells;
    if (s.move == MV_ARC_L || s.move == MV_ARC_R || s.move == MV_ARC_180) ++out.turns;
    if (s.move == MV_SPIN_L || s.move == MV_SPIN_R || s.move == MV_SPIN_180) ++out.spins;
  }
  out.seconds = route_time(out, r, d, start_head);
}

int route_cells(const Route &rt, int sx, int sy, Head start_head,
                RouteCellFn fn, void *ctx) {
  if (!rt.ok) return -1;
  int u = 2 * sx + 1, v = 2 * sy + 1;
  int h = (int)start_head, dd = 0;
  int n = 0;

  fn(ctx, sx, sy); ++n;

  for (int i = 0; i < rt.count; ++i) {
    const Step &s = rt.steps[i];

    if (s.diag) {
      // A diagonal step is one cell corner to corner, and the cell it crosses
      // is the one containing the midpoint -- (u+u2)>>2, not (u+u2-1)>>1,
      // which lands on the neighbour. That arithmetic cost a whole evening
      // once; it is the same expression diag_step() uses, deliberately.
      for (int k = 0; k < s.cells; ++k) {
        const int u2 = u + DXd[dd], v2 = v + DYd[dd];
        fn(ctx, (u + u2) >> 2, (v + v2) >> 2); ++n;
        u = u2; v = v2;
      }
    } else {
      for (int k = 0; k < s.cells; ++k) {
        u += 2 * HX[h]; v += 2 * HY[h];
        fn(ctx, (u - 1) >> 1, (v - 1) >> 1); ++n;
      }
    }

    switch (s.move) {
      case MV_ARC_L: case MV_SPIN_L:     h = (h + 3) & 3; break;
      case MV_ARC_R: case MV_SPIN_R:     h = (h + 1) & 3; break;
      case MV_ARC_180: case MV_SPIN_180: h = (h + 2) & 3; break;
      case MV_SD45_L: case MV_SD45_R: {
        // Onto the diagonal. She shifts to a wall midpoint of the cell she is
        // already standing in, so this enters no new cell and emits nothing.
        const int nd = (s.move == MV_SD45_L) ? ((h + 3) & 3) : h;
        u += DXd[nd] - HX[h];
        v += DYd[nd] - HY[h];
        dd = nd;
        break;
      }
      case MV_DS45_L: case MV_DS45_R: {
        // Off it, into the centre of the NEXT cell -- not the one the last
        // diagonal step crossed, so this one does count.
        const int nh = (s.move == MV_DS45_L) ? (int)diag_ccw(Diag(dd)) : (int)diag_cw(Diag(dd));
        u += HX[nh]; v += HY[nh];
        h = nh;
        fn(ctx, (u - 1) >> 1, (v - 1) >> 1); ++n;
        break;
      }
      case MV_DD90_L: dd = (dd + 3) & 3; break;
      case MV_DD90_R: dd = (dd + 1) & 3; break;
      case MV_GOAL:   return n;
      default:        return -1;
    }
  }
  return n;
}

int route_check(const Route &rt, const WallReader &maze,
                int sx, int sy, Head start_head, int gx, int gy, int gw, int gh) {
  if (!rt.ok) return -1;
  int u = 2 * sx + 1, v = 2 * sy + 1;
  int h = (int)start_head;     // orthogonal heading while !on_diag
  int dd = 0;                  // diagonal heading while on_diag
  bool on_diag = false;

  for (int i = 0; i < rt.count; ++i) {
    const Step &s = rt.steps[i];

    // --- the run before the turn ------------------------------------------
    if (s.diag) {
      if (!on_diag) return i + 1;
      for (int n = 0; n < s.cells; ++n) {
        int u2, v2, cx, cy;
        if (!diag_step(maze, u, v, dd, u2, v2, cx, cy)) return i + 1;
        u = u2; v = v2;
      }
    } else {
      if (on_diag) return i + 1;
      for (int n = 0; n < s.cells; ++n) {
        const int cx = (u - 1) >> 1, cy = (v - 1) >> 1;
        if (cx < 0 || cx >= W || cy < 0 || cy >= H) return i + 1;
        if (!maze.is_exit(maze.ctx, cx, cy, h)) return i + 1;
        u += 2 * HX[h]; v += 2 * HY[h];
      }
    }

    // --- the turn ----------------------------------------------------------
    switch (s.move) {
      case MV_GOAL: {
        const int cx = (u - 1) >> 1, cy = (v - 1) >> 1;
        if (on_diag) return i + 1;
        if (cx < gx || cx >= gx + gw || cy < gy || cy >= gy + gh) return i + 1;
        return 0;
      }
      case MV_ARC_L: case MV_SPIN_L:  if (on_diag) return i + 1; h = (h + 3) & 3; break;
      case MV_ARC_R: case MV_SPIN_R:  if (on_diag) return i + 1; h = (h + 1) & 3; break;
      case MV_ARC_180: case MV_SPIN_180: if (on_diag) return i + 1; h = (h + 2) & 3; break;
      case MV_SD45_L: case MV_SD45_R: {
        if (on_diag) return i + 1;
        const int nd = (s.move == MV_SD45_L) ? ((h + 3) & 3) : h;
        const int wu = u + (DXd[nd] - HX[h]);
        const int wv = v + (DYd[nd] - HY[h]);
        if (wall_index(wu, wv) < 0) return i + 1;
        const int cx = (u - 1) >> 1, cy = (v - 1) >> 1;
        if (!wall_open_for(maze, cx, cy, wu, wv)) return i + 1;
        u = wu; v = wv; dd = nd; on_diag = true;
        break;
      }
      case MV_DS45_L: case MV_DS45_R: {
        if (!on_diag) return i + 1;
        const int nh = (s.move == MV_DS45_L) ? (int)diag_ccw(Diag(dd)) : (int)diag_cw(Diag(dd));
        const int su = u + HX[nh], sv = v + HY[nh];
        if (!(su & 1) || !(sv & 1)) return i + 1;      // lands on a post
        const int ex = (su - 1) >> 1, ey = (sv - 1) >> 1;
        if (ex < 0 || ex >= W || ey < 0 || ey >= H) return i + 1;
        if (!wall_open_for(maze, ex, ey, u, v)) return i + 1;
        u = su; v = sv; h = nh; on_diag = false;
        break;
      }
      case MV_DD90_L: case MV_DD90_R: {
        if (!on_diag) return i + 1;
        dd = (s.move == MV_DD90_L) ? ((dd + 3) & 3) : ((dd + 1) & 3);
        break;
      }
      default: return i + 1;
    }
  }
  return 0;   // ran out of steps without a GOAL: still structurally fine
}

int route_points(const Route &rt, int sx, int sy, Head start_head,
                 RoutePointFn fn, void *ctx) {
  if (!rt.ok) return -1;
  int u = 2 * sx + 1, v = 2 * sy + 1;
  int h = (int)start_head, dd = 0;
  bool on_diag = false;
  int n = 0;

  fn(ctx, u, v, (int)MV_START); ++n;

  for (int i = 0; i < rt.count; ++i) {
    const Step &s = rt.steps[i];
    if (s.diag) { u += s.cells * DXd[dd];     v += s.cells * DYd[dd]; }
    else        { u += s.cells * 2 * HX[h];   v += s.cells * 2 * HY[h]; }

    switch (s.move) {
      case MV_ARC_L: case MV_SPIN_L:  h = (h + 3) & 3; break;
      case MV_ARC_R: case MV_SPIN_R:  h = (h + 1) & 3; break;
      case MV_ARC_180: case MV_SPIN_180: h = (h + 2) & 3; break;
      case MV_SD45_L: case MV_SD45_R: {
        const int nd = (s.move == MV_SD45_L) ? ((h + 3) & 3) : h;
        // Emit the centre BEFORE the shift, then the point on the diagonal:
        // an SD45 moves her sideways as well as turning her, and drawing it as
        // a single vertex would hide that.
        fn(ctx, u, v, (int)s.move); ++n;
        u += DXd[nd] - HX[h];
        v += DYd[nd] - HY[h];
        dd = nd; on_diag = true;
        fn(ctx, u, v, (int)MV_START); ++n;
        continue;
      }
      case MV_DS45_L: case MV_DS45_R: {
        const int nh = (s.move == MV_DS45_L) ? (int)diag_ccw(Diag(dd)) : (int)diag_cw(Diag(dd));
        fn(ctx, u, v, (int)s.move); ++n;
        u += HX[nh]; v += HY[nh];
        h = nh; on_diag = false;
        fn(ctx, u, v, (int)MV_START); ++n;
        continue;
      }
      case MV_DD90_L: dd = (dd + 3) & 3; break;
      case MV_DD90_R: dd = (dd + 1) & 3; break;
      case MV_GOAL:   fn(ctx, u, v, (int)MV_GOAL); return n + 1;
      default: return -1;
    }
    fn(ctx, u, v, (int)s.move); ++n;
  }
  (void)on_diag;
  return n;
}

}  // namespace plan
