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
#include "resize.h"
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
  }
  statistics *statistics = &solver->statistics;
  //printf("statistics->clauses_irredundant: %d\n", statistics->clauses_irredundant);
  if (!statistics->clauses_irredundant)
    return false;

  limits *limits = &solver->limits;
  //printf("limits->sym_break.conflicts: %lu\n", limits->sym_break.conflicts);
  //printf("statistics->conflicts: %lu\n", statistics->conflicts);
  if (limits->sym_break.conflicts <= statistics->conflicts)
    return true;
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

static void
break_symms_in_kissat(struct kissat * solver, BreakID* bid)
{
  kissat_message(solver, "Breaking cls: %d", breakid_get_num_break_cls(bid));
  kissat_message(solver, "Adding %d variables for symmetry breaking", breakid_get_num_aux_vars(bid));
  kissat_enlarge_variables(solver, VARS+breakid_get_num_aux_vars(bid)+1);

  int cls = 0;
  unsigned* brk = breakid_get_brk_cls(bid, &cls);
  while(cls > 0) {
   cls--;
   assert (EMPTY_STACK (solver->clause));
   printf("cl: \n");
   while(*brk != UINT_MAX) {
     //todo put the literals on the solver->clause stack
     printf("%d\n", IDX(*brk));
     PUSH_STACK(solver->clause, *brk);
     brk++;
   }
   printf("\n");
   brk++;
   kissat_message(solver, "adding sym breaking clause");
   kissat_new_irredundant_clause(solver);
 }
 CLEAR_STACK(solver->clause);
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
  breakid_set_verbosity(bid, kissat_verbosity(solver));
  breakid_set_useMatrixDetection(bid, true);
  breakid_set_symBreakingFormLength(bid, 50);
  breakid_set_steps_lim(bid,1000LL*1000LL);
  breakid_start_dynamic_cnf(bid, VARS);

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
      /* printf("Cl vars: "); */
      /* for(int i = 0; i < c->size; i++) printf("%d ", IDX(c->lits[i])); */
      /* printf("\n"); */
      breakid_add_clause(bid, (int*)c->lits, c->size);
    }

  // binary clauses
  int lits[2];
  for(int lit = 0; lit < 2*VARS; lit++) {

    watches *watches = all_watches + lit;
    lits[0] = lit;

    watch *const begin_watches = BEGIN_WATCHES (*watches);
    const watch *const end_watches = END_WATCHES (*watches);

    watch *p = begin_watches;

    unsigneds *const delayed = &solver->delayed;
    assert (EMPTY_STACK (*delayed));

    const size_t size_watches = SIZE_WATCHES (*watches);
    while (p != end_watches) {
      const watch head = *p++;
      const unsigned blocking = head.blocking.lit;
      //const value blocking_value = values[blocking];
      const bool binary = head.type.binary;

      if (!binary) {
        p++;
        continue;
      } else {
        assert (VALID_INTERNAL_LITERAL (blocking));
        lits[1] = blocking;
        if (lits[0] < lits[1]) {  //only do each binary once
          /* printf("Bin vars: %d %d\n", IDX(lits[0]), IDX(lits[1])); */
          breakid_add_clause(bid, lits, 2);
        }
      }
    }
  }
  breakid_end_dynamic_cnf(bid);
  kissat_message(solver, "Steps remain: %ld\n",breakid_get_steps_remain(bid));
  kissat_message(solver, "Num generators: %d\n", breakid_get_num_generators(bid));
  if (kissat_verbosity (solver) >= 2) breakid_print_generators(bid);
  breakid_detect_subgroups(bid);
  if (kissat_verbosity (solver) >= 2) breakid_print_subgroups(bid);
  breakid_break_symm(bid);
  if (breakid_get_num_break_cls(bid) != 0) break_symms_in_kissat(solver, bid);

  // Finish up
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

