// Native harness: load a competition .maz, plan SHORTEST and QUICKEST, compare.
#include "planner.h"
#include "diagonal.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace plan;

struct MazeFile {
  uint8_t cell[16][16] = {};   // bit0 N, bit1 E, bit2 S, bit3 W  (1 = wall)
  bool load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t buf[256];
    size_t n = fread(buf, 1, 256, f);
    fclose(f);
    if (n != 256) return false;
    for (int x = 0; x < 16; ++x)
      for (int y = 0; y < 16; ++y) cell[x][y] = buf[x * 16 + y];
    return true;
  }
  static bool exit_fn(const void *ctx, int x, int y, int h) {
    const MazeFile *m = (const MazeFile *)ctx;
    if (x < 0 || x > 15 || y < 0 || y > 15) return false;
    return (m->cell[x][y] & (1 << h)) == 0;
  }
  WallReader reader() const { return WallReader{&MazeFile::exit_fn, this}; }
};

static Route g_short, g_quick;   // 1.2 KB each -- static, not on the stack

static const char *mv_name(Move m) {
  switch (m) {
    case MV_START: return "START";
    case MV_ARC_L: return "SS90L";
    case MV_ARC_R: return "SS90R";
    case MV_ARC_180: return "SS180";
    case MV_SPIN_L: return "SPIN_L";
    case MV_SPIN_R: return "SPIN_R";
    case MV_SPIN_180: return "SPIN_180";
    case MV_SD45_L: return "SD45L";
    case MV_SD45_R: return "SD45R";
    case MV_DS45_L: return "DS45L";
    case MV_DS45_R: return "DS45R";
    case MV_DD90_L: return "DD90L";
    case MV_DD90_R: return "DD90R";
    case MV_GOAL: return "GOAL";
  }
  return "?";
}

int main(int argc, char **argv) {
  Robot r;
  bool verbose = false;
  std::vector<std::string> files;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-v") verbose = true;
    else if (a == "--no180") r.allow_arc180 = false;
    else if (a.rfind("--vmax=", 0) == 0) r.straight.v_max = atof(a.c_str() + 7);
    else if (a.rfind("--accel=", 0) == 0) { r.straight.accel = atof(a.c_str() + 8); r.straight.decel = r.straight.accel; }
    else files.push_back(a);
  }

  printf("v_max %.0f mm/s   accel %.0f mm/s2   arc90 %.0f mm/s (omega %.0f, alpha %.0f)   "
         "spin (omega %.0f, alpha %.0f)\n\n",
         r.straight.v_max, r.straight.accel, r.arc90_speed, r.arc90_omega, r.arc90_alpha,
         r.spin_omega, r.spin_alpha);

  printf("%-22s %8s %8s %8s %6s %6s\n",
         "maze", "short_s", "quick_s", "diag_s", "subs", "saved");
  printf("%s\n", std::string(64, '-').c_str());

  for (const std::string &f : files) {
    MazeFile m;
    if (!m.load(f.c_str())) { printf("%-26s  (unreadable)\n", f.c_str()); continue; }

    const WallReader wr = m.reader();
    plan_route(g_short, wr, r, SHORTEST, 0, 0, NN, 7, 7, 2, 2);
    plan_route(g_quick, wr, r, QUICKEST, 0, 0, NN, 7, 7, 2, 2);
    const Route &s = g_short; const Route &q = g_quick;

    std::string name = f.substr(f.find_last_of('/') + 1);
    if (!s.ok || !q.ok) { printf("%-26s  NO ROUTE (short %d quick %d)\n", name.c_str(), s.ok, q.ok); continue; }

    DiagTurns dt;
    static Route g_diag;
    g_diag = g_quick;
    const int subs = diagonalise(g_diag, r, dt, NN);
    const float t_diag = route_time(g_diag, r, dt, NN);

    printf("%-22s %8.3f %8.3f %8.3f %6d %5.1f%%\n",
           name.c_str(), s.seconds, q.seconds, t_diag, subs,
           100.0 * (q.seconds - t_diag) / q.seconds);

    if (verbose) {
      printf("   DIAGONAL route:\n");
      for (int i=0;i<g_diag.count;++i) { const Step &st=g_diag.steps[i];
        printf("      %-9s %-5s %2d  -> (%2d,%2d)\n", mv_name(st.move), st.diag?"diag":"cells", st.cells, st.x, st.y); }
    }
  }
  return 0;
}
