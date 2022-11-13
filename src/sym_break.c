#include "allocate.h"
#include "backtrack.h"
#include "collect.h"
#include "dense.h"
#include "eliminate.h"
#include "forward.h"
#include "inline.h"
#include "kitten.h"
#include "propdense.h"
#include "print.h"
#include "report.h"
#include "resolve.h"
#include "terminate.h"
#include "trail.h"
#include "weaken.h"

#include <inttypes.h>
#include <math.h>

static uint64_t
sym_break_adjustment (kissat * solver)
{
  return 2 * CLAUSES + NLOGN (1 + solver->active);
}

bool
kissat_sym_breaking (kissat * solver)
{
  if (!GET_OPTION(symbreak)) {
    return false;
  statistics *statistics = &solver->statistics;
  if (!statistics->clauses_irredundant)
    return false;

  limits *limits = &solver->limits;
  if (limits->sym_break.conflicts > statistics->conflicts)
    return true;
  }
  return false;
}

int kissat_sym_break (struct kissat *)
{
}

