// Find, for each turn we have to tune, the bench maze that shows it best.
//
// A tuning maze should be a single corridor: no junctions, so there is exactly
// one route and the planner cannot wander off and test something else. That
// makes the search space the self-avoiding walks from the start cell (0,0)
// rather than every wall layout, which is both far smaller and exactly the set
// of mazes worth building.
//
// Section size is a parameter because James has four 3x3 sections, and two of
// them butted together give 6x3 or 3x6.
#include "planner.h"
#include "diagonal.h"
#include "native.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
using namespace plan;

static int SECW = 3, SECH = 3;

struct Sec {
  uint8_t cell[16][16];                 // bit0 N bit1 E bit2 S bit3 W, set = wall
  void seal() {
    for (int x = 0; x < 16; x++)
      for (int y = 0; y < 16; y++) cell[x][y] = 0x0F;
  }
  void open_between(int x, int y, int h) {
    static const int dx[4] = {0, 1, 0, -1}, dy[4] = {1, 0, -1, 0};
    cell[x][y] &= ~(1 << h);
    int nx = x + dx[h], ny = y + dy[h];
    if (nx >= 0 && nx < 16 && ny >= 0 && ny < 16) cell[nx][ny] &= ~(1 << ((h + 2) & 3));
  }
  static bool ex(const void *c, int x, int y, int h) {
    const Sec *m = (const Sec *)c;
    if (x < 0 || x > 15 || y < 0 || y > 15) return false;
    return (m->cell[x][y] & (1 << h)) == 0;
  }
  WallReader reader() const { return WallReader{&Sec::ex, this}; }
};

static const char *mv(Move m) {
  switch (m) {
    case MV_START: return "START";   case MV_ARC_L: return "ARC_L";
    case MV_ARC_R: return "ARC_R";   case MV_ARC_180: return "ARC_180";
    case MV_SPIN_L: return "SPIN_L"; case MV_SPIN_R: return "SPIN_R";
    case MV_SPIN_180: return "SPIN_180";
    case MV_SD45_L: return "SD45_L"; case MV_SD45_R: return "SD45_R";
    case MV_DS45_L: return "DS45_L"; case MV_DS45_R: return "DS45_R";
    case MV_DD90_L: return "DD90_L"; case MV_DD90_R: return "DD90_R";
    case MV_GOAL: return "GOAL";
  }
  return "?";
}

struct Hit {
  bool set = false;
  std::string walk, route;
  int gx = 0, gy = 0, hits = 0, others = 99, cells = -1, turns = 99;
  float seconds = 0;
  // Most repetitions of the turn under test first -- a turn you see three times
  // in one run tells you more than one you see once. Then fewest other kinds of
  // turn, so the maze isolates it. Then most cells, for the longest run-up.
  bool beats(int h, int o, int c, int t) const;
};

// --isolate ranks a rig by how little else is in it before how often the turn
// under test appears. That is the maze you want when you are tuning a single
// profile; the default ranking is the one you want when you are looking for
// the hardest run the section can produce.
static bool isolate = false;

bool Hit::beats(int h, int o, int c, int t) const {
  if (isolate) {
    if (o != others) return o < others;
    if (h != hits) return h > hits;
  } else {
    if (h != hits) return h > hits;
    if (o != others) return o < others;
  }
  if (c != cells) return c > cells;
  return t < turns;
}

static Hit best[MV_GOAL + 1];
static Robot robot;
static DiagTurns dt;
static long walks = 0;

static const int DX[4] = {0, 1, 0, -1}, DY[4] = {1, 0, -1, 0};
static const char HC[4] = {'N', 'E', 'S', 'W'};

static void score(const std::vector<int> &dir, int gx, int gy) {
  Sec m;
  m.seal();
  int x = 0, y = 0;
  std::string walk;
  for (size_t i = 0; i < dir.size(); i++) {
    m.open_between(x, y, dir[i]);
    walk += HC[dir[i]];
    x += DX[dir[i]];
    y += DY[dir[i]];
  }
  Route rt;
  plan_native(rt, m.reader(), robot, dt, QUICKEST, 0, 0, NN, gx, gy, 1, 1);
  if (!rt.ok) return;
  if (route_check(rt, m.reader(), 0, 0, NN, gx, gy, 1, 1) != 0) return;

  int seen[MV_GOAL + 1] = {0};
  std::string line;
  for (int i = 0; i < rt.count; i++) {
    Move mm = rt.steps[i].move;
    seen[mm]++;
    // A spin means she stops dead mid-test: nothing is being tuned there.
    if (mm == MV_SPIN_L || mm == MV_SPIN_R || mm == MV_SPIN_180 || mm == MV_ARC_180) return;
    if (i) line += " ";
    line += mv(mm);
  }
  for (int t = MV_ARC_L; t < MV_GOAL; t++) {
    if (!seen[t]) continue;
    int others = 0;
    for (int k = MV_ARC_L; k < MV_GOAL; k++)
      if (k != t && seen[k]) others++;
    Hit &b = best[t];
    if (!b.set || b.beats(seen[t], others, rt.cells, rt.turns)) {
      b.set = true; b.walk = walk; b.route = line; b.gx = gx; b.gy = gy;
      b.hits = seen[t]; b.others = others; b.cells = rt.cells;
      b.turns = rt.turns; b.seconds = rt.seconds;
    }
  }
}

static bool used[16][16];

static void walk_from(int x, int y, std::vector<int> &dir) {
  if (!dir.empty()) { walks++; score(dir, x, y); }
  if ((int)dir.size() >= SECW * SECH - 1) return;
  for (int h = 0; h < 4; h++) {
    int nx = x + DX[h], ny = y + DY[h];
    if (nx < 0 || ny < 0 || nx >= SECW || ny >= SECH) continue;
    if (used[nx][ny]) continue;
    used[nx][ny] = true;
    dir.push_back(h);
    walk_from(nx, ny, dir);
    dir.pop_back();
    used[nx][ny] = false;
  }
}

int main(int argc, char **argv) {
  float off = 60.0f;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a.rfind("--vmax=", 0) == 0) robot.straight.v_max = atof(a.c_str() + 7);
    else if (a.rfind("--off=", 0) == 0) off = atof(a.c_str() + 6);
    else if (a.rfind("--size=", 0) == 0) sscanf(a.c_str() + 7, "%dx%d", &SECW, &SECH);
    else if (a == "--isolate") isolate = true;
  }
  dt.sd45_offset = dt.ds45_offset = off;

  memset(used, 0, sizeof(used));
  used[0][0] = true;
  std::vector<int> dir;
  walk_from(0, 0, dir);

  printf("section %dx%d   %ld single-corridor mazes   offset %.0f   v_max %.0f\n\n",
         SECW, SECH, walks, off, robot.straight.v_max);
  for (int t = MV_ARC_L; t < MV_GOAL; t++) {
    Hit &b = best[t];
    if (!b.set) {
      printf("%-9s  -- not reachable in a %dx%d from the standard start --\n\n",
             mv((Move)t), SECW, SECH);
      continue;
    }
    printf("%-9s  x%d   goal (%d,%d)   %d cells   %.3f s\n",
           mv((Move)t), b.hits, b.gx, b.gy, b.cells, b.seconds);
    printf("           other turn kinds in the route: %d\n", b.others);
    printf("           corridor from (0,0): %s\n", b.walk.c_str());
    printf("           route: %s\n\n", b.route.c_str());
  }
  return 0;
}
