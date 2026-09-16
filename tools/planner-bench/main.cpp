// Native harness: load a competition .maz, plan SHORTEST and QUICKEST, compare.
#include "planner.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace plan;

struct MazeFile : WallReader {
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
  bool is_exit(int x, int y, int h) const override {
    if (x < 0 || x > 15 || y < 0 || y > 15) return false;
    return (cell[x][y] & (1 << h)) == 0;
  }
};

static const char *mv_name(Move m) {
  switch (m) {
    case MV_START: return "START";
    case MV_ARC_L: return "SS90L";
    case MV_ARC_R: return "SS90R";
    case MV_ARC_180: return "SS180";
    case MV_SPIN_L: return "SPIN_L";
    case MV_SPIN_R: return "SPIN_R";
    case MV_SPIN_180: return "SPIN_180";
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

  printf("%-26s %7s %6s %6s %5s   %7s %6s %6s %5s   %7s\n",
         "maze", "short_s", "cells", "arcs", "spins", "quick_s", "cells", "arcs", "spins", "saved");
  printf("%s\n", std::string(104, '-').c_str());

  for (const std::string &f : files) {
    MazeFile m;
    if (!m.load(f.c_str())) { printf("%-26s  (unreadable)\n", f.c_str()); continue; }

    Route s = plan_route(m, r, SHORTEST, 0, 0, NN, 7, 7, 2, 2);
    Route q = plan_route(m, r, QUICKEST, 0, 0, NN, 7, 7, 2, 2);

    std::string name = f.substr(f.find_last_of('/') + 1);
    if (!s.ok || !q.ok) { printf("%-26s  NO ROUTE (short %d quick %d)\n", name.c_str(), s.ok, q.ok); continue; }

    printf("%-26s %7.3f %6d %6d %5d   %7.3f %6d %6d %5d   %6.1f%%\n",
           name.c_str(), s.seconds, s.cells, s.turns, s.spins,
           q.seconds, q.cells, q.turns, q.spins,
           100.0 * (s.seconds - q.seconds) / s.seconds);

    if (verbose) {
      printf("   QUICKEST route:\n");
      for (int i=0;i<q.count;++i) { const Step &st=q.steps[i];
        printf("      %-9s cells %2d  -> (%2d,%2d)  %.3f s\n", mv_name(st.move), st.cells, st.x, st.y, st.t); }
    }
  }
  return 0;
}
