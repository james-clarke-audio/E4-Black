// Read a bench maze back out of the ASCII file we are actually going to ship,
// and plan it. The point is to close the loop on the delivered artifact rather
// than on the in-memory maze the generator held: if the writer got a wall or a
// goal wrong, this is where it shows.
#include "planner.h"
#include "diagonal.h"
#include "native.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
using namespace plan;

struct Txt {
  uint8_t cell[16][16];
  int gx = -1, gy = -1, goals = 0;

  bool load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    std::vector<std::string> L;
    char buf[512];
    while (fgets(buf, sizeof buf, f)) {
      std::string s(buf);
      while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
      L.push_back(s);
    }
    fclose(f);
    if (L.size() < 33) return false;
    auto at = [&](int r, int c) -> char {
      return (r < (int)L.size() && c < (int)L[r].size()) ? L[r][c] : ' ';
    };
    for (int x = 0; x < 16; x++) {
      for (int y = 0; y < 16; y++) {
        int row = (15 - y) * 2 + 1;           // the body row for this cell
        int col = x * 4;                      // its west wall column
        uint8_t w = 0;
        if (at(row - 1, col + 1) == '-') w |= 1;   // north
        if (at(row, col + 4) == '|')     w |= 2;   // east
        if (at(row + 1, col + 1) == '-') w |= 4;   // south
        if (at(row, col) == '|')         w |= 8;   // west
        cell[x][y] = w;
        if (at(row, col + 2) == 'G') { gx = x; gy = y; goals++; }
      }
    }
    return true;
  }
  static bool ex(const void *c, int x, int y, int h) {
    const Txt *m = (const Txt *)c;
    if (x < 0 || x > 15 || y < 0 || y > 15) return false;
    return (m->cell[x][y] & (1 << h)) == 0;
  }
  WallReader reader() const { return WallReader{&Txt::ex, this}; }
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

int main(int argc, char **argv) {
  Robot r;
  DiagTurns d;
  float off = 60.0f;
  Objective kind = QUICKEST;
  std::vector<const char *> files;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a.rfind("--vmax=", 0) == 0) r.straight.v_max = atof(a.c_str() + 7);
    else if (a.rfind("--off=", 0) == 0) off = atof(a.c_str() + 6);
    else files.push_back(argv[i]);
  }
  d.sd45_offset = d.ds45_offset = off;
  printf("offset %.0f   v_max %.0f\n\n", off, r.straight.v_max);

  int bad = 0;
  Route rt;
  for (const char *p : files) {
    Txt m;
    if (!m.load(p)) { printf("%-24s  CANNOT READ\n", p); bad++; continue; }
    const char *base = strrchr(p, '/');
    base = base ? base + 1 : p;
    if (m.goals != 1) { printf("%-24s  %d goal cells, expected 1\n", base, m.goals); bad++; continue; }
    plan_native(rt, m.reader(), r, d, kind, 0, 0, NN, m.gx, m.gy, 1, 1);
    if (!rt.ok) { printf("%-24s  NO ROUTE to (%d,%d)\n", base, m.gx, m.gy); bad++; continue; }
    int chk = route_check(rt, m.reader(), 0, 0, NN, m.gx, m.gy, 1, 1);
    printf("%-24s goal (%d,%d)  %2d cells  %6.3f s  check %d\n", base, m.gx, m.gy,
           rt.cells, rt.seconds, chk);
    for (int i = 0; i < rt.count; i++) {
      const Step &s = rt.steps[i];
      // The leg BEFORE each turn: how far she runs, and whether that run is
      // along cell centres or along the diagonal.
      printf("%-24s   %-8s %2d %s\n", "", mv(s.move), s.cells,
             s.diag ? "diagonal steps" : "cells");
    }
    printf("\n");
    if (chk != 0) bad++;
  }
  printf(bad ? "%d FAILED\n" : "all good\n", bad);
  return bad ? 1 : 0;
}
