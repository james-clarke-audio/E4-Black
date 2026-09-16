/******************************************************************************
 * native.h -- diagonal route planning as a search, not a substitution.
 *
 * diagonal.cpp improves the route it is handed. This one searches a space the
 * orthogonal planner cannot express, so it can choose a LONGER cell route
 * because that route diagonalises better -- which is the one thing post-
 * processing can never do, since the orthogonal planner discarded those routes
 * before the substitution pass ever ran.
 *
 * THE LATTICE. Work in half-cells: u = x/90, v = y/90, both 0..32. Parity then
 * says what a point is:
 *
 *   (odd, odd)    cell centre        cell (x,y) is at (2x+1, 2y+1)
 *   (even, odd)   vertical-wall midpoint      \  the diagonal runs
 *   (odd, even)   horizontal-wall midpoint    /  through these
 *   (even, even)  a POST -- never occupied
 *
 * A diagonal is a straight line through wall midpoints, stepping (+-1, +-1) so
 * the parity alternates between the two wall kinds and never lands on a post.
 * It is NOT the line joining cell centres diagonally: that passes straight
 * through a post. Running NE out of cell (0,1) the mouse joins the line
 * y = x + 90, through (180,270), (270,360), (360,450) -- wall midpoints, one
 * step of 90*sqrt(2) = 127.28 mm apart, clear of every post.
 *
 * THE TWO TRANSITIONS, and they are the same offset in each direction:
 *
 *   SD45  centre (2x+1, 2y+1)  ->  wall midpoint at (2x+1, 2y+1) + (d - h)
 *   DS45  wall midpoint (u,v)  ->  centre        at (u, v)       + (d - h)
 *
 * where h is the orthogonal heading and d the diagonal one, so (d - h) is the
 * unit orthogonal step 45 degrees from h towards d. Heading N and turning onto
 * NE, (d-h) = E: she joins the diagonal at the EAST wall of the cell she is in.
 *
 * WALLS. A diagonal step crosses one cell corner to corner, so it needs the
 * wall it enters by and the wall it leaves by to both be openings of that cell.
 * The cell is the one containing the midpoint of the step.
 *
 * SIZE. 2048 centre nodes (16*16 cells * 4 headings * 2 speed classes) and
 * 2176 diagonal nodes (544 wall midpoints * 4 diagonal headings), 4224 in all.
 * Spin has no meaning on a diagonal, so the diagonal half carries no speed
 * class.
 *****************************************************************************/
#pragma once

#include "planner.h"
#include "diagonal.h"

namespace plan {

/// Diagonal headings, in the same rotational order as Head.
enum Diag : uint8_t { NE = 0, SE = 1, SW = 2, NW = 3 };

constexpr int N_CENTRE = W * H * NHEAD * NSPEED;    // 2048
constexpr int N_VWALL  = (W + 1) * H;               // 272 vertical wall midpoints
constexpr int N_HWALL  = W * (H + 1);               // 272 horizontal
constexpr int N_DIAG   = (N_VWALL + N_HWALL) * 4;   // 2176
constexpr int N_NATIVE = N_CENTRE + N_DIAG;         // 4224

/// Plan allowing diagonals natively. `d.enabled` false makes it equivalent to
/// the orthogonal planner, which is the comparison worth having: the native
/// search must never be slower than classic substitution on the same maze,
/// because everything classic can build is inside the space this searches.
void plan_native(Route &out, const WallReader &maze, const Robot &r,
                 const DiagTurns &d, Objective obj,
                 int sx, int sy, Head start_head,
                 int gx, int gy, int gw = 2, int gh = 2);

/// Walk a route over the lattice and check every move against the map,
/// independently of whichever planner produced it. Returns 0 if drivable, or
/// the 1-based index of the first step that is not, so a failure names itself.
///
/// This exists because two planners disagreed and neither could be trusted to
/// adjudicate. A planner that scores its own output is only ever as honest as
/// its own model.
int route_check(const Route &rt, const WallReader &maze,
                int sx, int sy, Head start_head, int gx, int gy, int gw, int gh);

/// Walk a route and hand back its vertices in HALF-CELLS, with the move made
/// at each. Half-cells are the natural unit to send to a companion app: a
/// diagonal is then just another line segment between two points, and the app
/// needs to know nothing about the lattice, the parity rule or which wall is
/// which. Cell centres come out on odd coordinates, wall midpoints on mixed.
///
/// `fn` is called for the start, for every turn, and for the goal. Returns the
/// number of points emitted, or -1 if the route is not walkable.
typedef void (*RoutePointFn)(void *ctx, int u, int v, int move);
int route_points(const Route &rt, int sx, int sy, Head start_head,
                 RoutePointFn fn, void *ctx);

}  // namespace plan
