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

#include <breakid/breakid_c.h>
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
  if (!GET_OPTION(sym_break)) {
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

static void do_breakid (struct kissat * solver);

static
void sym_break (struct kissat * solver)
{
  kissat_backtrack_propagate_and_flush_trail (solver);
  assert (!solver->inconsistent);
  STOP_SEARCH_AND_START_SIMPLIFIER (sym_break);
  kissat_phase (solver, "sym_break", GET (eliminations),
		"elimination limit of %" PRIu64 " conflicts hit",
		solver->limits.sym_break.conflicts);
  do_breakid (solver);
  kissat_check_statistics (solver);
  STOP_SIMPLIFIER_AND_RESUME_SEARCH (sym_break);
}

static
void do_breakid (struct kissat * solver)
{
  LOG ("adding CNF to breakid");

  clause *last_irredundant = kissat_last_irredundant_clause (solver);

  const value *const values = solver->values;
  watches *all_watches = solver->watches;
  ward *const arena = BEGIN_STACK (solver->arena);
  BreakID* bid = breakid_new();
  breakid_set_verbosity(bid, 3);

  // Long irredundant clauses
  for (all_clauses (c))
    {
      if (last_irredundant && c > last_irredundant)
	break;
      if (c->redundant)
	continue;
      if (c->garbage)
	continue;
      bool satisfied = false;
      assert (!solver->level);
      for (all_literals_in_clause (lit, c))
	{
	  const value value = values[lit];
	  if (value <= 0)
	    continue;
	  satisfied = true;
	  break;
	}
      if (satisfied)
	{
	  kissat_mark_clause_as_garbage (solver, c);
	  continue;
	}
      const reference ref = (ward *) c - arena;
      assert (ref == kissat_reference_clause (solver, c));
      assert (c == kissat_dereference_clause (solver, ref));
      breakid_start_dynamic_cnf(bid, VARS);
      breakid_add_clause(bid, (int*)c->lits, c->size);
    }

  // binary clauses
  int lits[2];
  for(int lit = 1; lit <= VARS; lit++) {
    watches *watches = all_watches + lit;
    lits[0] = lit;

    watch *const begin_watches = BEGIN_WATCHES (*watches);
    const watch *const end_watches = END_WATCHES (*watches);

    watch *p = begin_watches;

    unsigneds *const delayed = &solver->delayed;
    assert (EMPTY_STACK (*delayed));

    const size_t size_watches = SIZE_WATCHES (*watches);
    while (p != end_watches) {
      p++;
      const watch head = *p++;
      const unsigned blocking = head.blocking.lit;
      assert (VALID_INTERNAL_LITERAL (blocking));
      //const value blocking_value = values[blocking];
      const bool binary = head.type.binary;

      if (!binary) {
        p++;
        continue;
      } else {
        lits[1] = blocking;
        breakid_add_clause(bid, lits, 2);
      }
    }
  }
  breakid_end_dynamic_cnf(bid);
  //int64_t remain = breakid.get_steps_remain();
  breakid_print_generators(bid);

  breakid_del(bid);
}

int kissat_sym_break (struct kissat * solver)
{
  assert (!solver->inconsistent);
  INC (sym_breaks);
  sym_break (solver);
  UPDATE_CONFLICT_LIMIT (sym_break, sym_breaks, NLOG2N, true);

  return solver->inconsistent ? 20 : 0;
}

