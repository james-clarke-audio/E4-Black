/******************************************************************************
 * diagonal.h -- turning an orthogonal route into a diagonal one.
 *
 * THE CLASSIC METHOD. Plan the route on cells as if diagonals did not exist,
 * then look through the result for zigzags -- runs of alternating 90 degree
 * turns in consecutive cells -- and replace each with a straight line at 45
 * degrees: SD45 onto the diagonal, a diagonal run, DS45 back off it.
 *
 * WHY THIS IS NOT A BLIND PEEPHOLE. The obvious implementation substitutes
 * wherever the pattern matches. That is wrong at both ends: a two-turn zigzag
 * is rarely worth leaving the centreline for, and a long one is worth it even
 * when the arithmetic looks marginal. So each candidate is COSTED BOTH WAYS
 * with the same model the planner uses, and substituted only if the diagonal
 * actually wins. That also makes this a fair opponent for the native diagonal
 * search rather than a straw man.
 *
 * WHAT IT CANNOT DO, and this is the whole reason the native search exists:
 * it can only improve the route it was given. If the genuinely fastest line
 * through the maze is a longer cell route that happens to diagonalise
 * beautifully, no amount of post-processing will find it, because the
 * orthogonal planner threw it away before this code ever ran.
 *
 * GEOMETRY. A diagonal step is one cell corner to the next, 180/sqrt(2) =
 * 127.28 mm, against 180 mm for an orthogonal one. k alternating turns in
 * consecutive cells cover k-1 diagonal steps, and the exit turn takes the same
 * hand as the last turn of the zigzag -- which makes the net heading change
 * come out right for both odd and even k without a special case.
 *****************************************************************************/
#pragma once

#include "planner.h"

namespace plan {

constexpr float DIAG_PITCH = 127.279f;   // 180 / sqrt(2)

/// The diagonal rows of the turn table. Separate from Robot so that the
/// orthogonal planner stays usable with diagonals switched off entirely.
struct DiagTurns {
  // Straight onto the diagonal, and back off it. Same angle, mirrored
  // geometry, different entry and exit offsets -- one set of numbers cannot
  // serve both, and using it for both is how a mouse ends up half a cell out
  // only on alternate corners.
  float sd45_speed  = 300.0f;
  float sd45_offset = 120.0f;
  float sd45_omega  = 95.0f;
  float sd45_alpha  = 2500.0f;

  float ds45_speed  = 300.0f;
  float ds45_offset = 120.0f;
  float ds45_omega  = 95.0f;
  float ds45_alpha  = 2500.0f;

  /// Top speed along a diagonal. Usually below the straight-line v_max: the
  /// diagonal corridor is narrower than a cell and she is threading posts.
  float diag_v_max = 300.0f;

  bool enabled = true;
};

/// Rewrite `r` in place, replacing zigzags with diagonals wherever that is
/// faster. Returns the number of substitutions made.
int diagonalise(Route &r, const Robot &rob, const DiagTurns &d, Head start_head);

/// Total seconds for a route, orthogonal or diagonal. The planner's own figure
/// is only valid for the route it produced; once diagonalise() has rewritten
/// it, the time has to be recomputed or it is a leftover from before the
/// substitution -- which is exactly the kind of number that reads as a
/// measurement and is not one.
float route_time(const Route &r, const Robot &rob, const DiagTurns &d, Head start_head);

/// The same walk as route_time(), reporting each step as it goes: how long the
/// RUN before the turn takes, and how long the TURN itself takes.
///
/// It exists so that anything which executes a route -- the speed run, and the
/// simulator that rehearses one -- gets its timing from the model that costed
/// the route rather than from a second model written alongside it. Two models
/// of one mouse drift, and the drift shows up as a route that takes longer
/// than the planner promised for reasons nobody can name.
///
/// The durations sum to route_time()'s total by construction: both call the
/// same step_time().
typedef void (*RouteStepFn)(void *ctx, int index, const Step &s,
                            float run_seconds, float turn_seconds);
void route_times(const Route &r, const Robot &rob, const DiagTurns &d,
                 Head start_head, RouteStepFn fn, void *ctx);

}  // namespace plan
