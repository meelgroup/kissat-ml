#include "allocate.h"
#include "collect.h"
#include "inline.h"

#include <string.h>

static void
inc_clause (kissat * solver, bool original, bool redundant)
{
  if (redundant)
    INC (clauses_redundant);
  else
    INC (clauses_irredundant);
  INC (clauses_added);
  if (original)
    INC (clauses_original);
}

static void
dec_clause (kissat * solver, bool redundant)
{
  if (redundant)
    DEC (clauses_redundant);
  else
    DEC (clauses_irredundant);
}

static void
init_clause (kissat * solver, clause * res,
	     bool redundant, unsigned glue, unsigned size)
{
  assert (size <= UINT_MAX);
  assert (redundant || !glue);

  glue = MIN (MAX_GLUE, glue);

  const unsigned tier1 = GET_OPTION (tier1);
  bool keep = (glue <= tier1);
  if (redundant && GET_OPTION(usemldata)) keep = false;

  res->glue = glue;

  res->garbage = false;
  if (redundant && GET_OPTION(usemldata)) assert(!keep);
  res->keep = keep;
  res->reason = false;
  res->redundant = redundant;
  res->shrunken = false;
  res->subsume = false;
  res->sweeped = false;
  res->vivify = false;

  res->used = 0;
  res->cl_id = solver->cl_id++;
  res->props_used = 0;
  res->uip1_used = 0;
  res->last_touched = CONFLICTS;
  if (!redundant || !IS_ML) res->extra_data_idx = -1;
  else {
    extdata d;
    d.cl_id = res->cl_id;
    d.garbage = false;
    d.clause_born = CONFLICTS;
    d.cl_ref = res;
    memset(d.discounted_props_used, 0, sizeof(d.discounted_props_used));
    memset(d.discounted_uip1_used, 0, sizeof(d.discounted_uip1_used));
    d.sum_props_used = 0;
    d.sum_uip1_used = 0;
    memset(d.pred_lev, 0, sizeof(d.pred_lev));
    d.sum_props_used_per_time_rank_rel = -1;
    d.sum_props_used_per_time_rank_rel = -1;
    d.last_touched_rank_rel = -1;
    PUSH_STACK(solver->extra_data, d);
    res->extra_data_idx = SIZE_STACK(solver->extra_data)-1;
  }

  res->searched = 2;
  res->size = size;
#ifdef NOPTIONS
  (void) solver;
#endif
}

void
kissat_connect_clause (kissat * solver, clause * c)
{
  watches *all_watches = solver->watches;
  const reference ref = kissat_reference_clause (solver, c);
  kissat_inlined_connect_clause (solver, all_watches, c, ref);
}

static reference
new_binary_clause (kissat * solver,
		   bool original, bool redundant,
		   unsigned first, unsigned second)
{
  assert (first != second);
  assert (first != NOT (second));
  assert (!original || !redundant);
  kissat_watch_binary (solver, redundant, first, second);
  if (!redundant)
    {
      kissat_mark_added_literal (solver, first);
      kissat_mark_added_literal (solver, second);
    }
  inc_clause (solver, original, redundant);
  if (!original)
    {
      CHECK_AND_ADD_BINARY (first, second);
      ADD_BINARY_TO_PROOF (first, second);
    }
  return INVALID_REF;
}

static reference
new_large_clause (kissat * solver,
		  bool original, bool redundant, unsigned glue,
		  unsigned size, unsigned *lits)
{
  assert (size > 2);
  reference res = kissat_allocate_clause (solver, size);
  clause *c = kissat_unchecked_dereference_clause (solver, res);
  init_clause (solver, c, redundant, glue, size);
  memcpy (c->lits, lits, size * sizeof (unsigned));
  LOGREF (res, "new");
  if (solver->watching)
    kissat_watch_reference (solver, lits[0], lits[1], res);
  else
    kissat_connect_clause (solver, c);
  if (redundant)
    {
      if (!c->keep && solver->first_reducible == INVALID_REF)
	solver->first_reducible = res;
    }
  else
    {
      kissat_mark_added_literals (solver, size, lits);
      solver->last_irredundant = res;
    }
  inc_clause (solver, original, redundant);
  if (!original)
    {
      CHECK_AND_ADD_CLAUSE (c);
      ADD_CLAUSE_TO_PROOF (c);
    }
  return res;
}

static reference
new_clause (kissat * solver,
	    bool original, bool redundant,
	    unsigned glue, unsigned size, unsigned *lits)
{
  reference res;
  if (size == 2)
    res = new_binary_clause (solver, original, redundant, lits[0], lits[1]);
  else
    res = new_large_clause (solver, original, redundant, glue, size, lits);
  kissat_defrag_watches_if_needed (solver);
  return res;
}

void
kissat_new_binary_clause (kissat * solver,
			  bool redundant, unsigned first, unsigned second)
{
  (void) new_binary_clause (solver, false, redundant, first, second);
}

reference
kissat_new_original_clause (kissat * solver)
{
  const unsigned size = SIZE_STACK (solver->clause);
  unsigned *lits = BEGIN_STACK (solver->clause);
  kissat_sort_literals (solver, size, lits);
  reference res = new_clause (solver, true, false, 0, size, lits);
  return res;
}

reference
kissat_new_irredundant_clause (kissat * solver)
{
  const unsigned size = SIZE_STACK (solver->clause);
  unsigned *lits = BEGIN_STACK (solver->clause);
  return new_clause (solver, false, false, 0, size, lits);
}

reference
kissat_new_redundant_clause (kissat * solver, unsigned glue)
{
  const unsigned size = SIZE_STACK (solver->clause);
  unsigned *lits = BEGIN_STACK (solver->clause);
  reference ref = new_clause (solver, false, true, glue, size, lits);
  if (ref != INVALID_REF) {
    clause *c = kissat_dereference_clause (solver, ref);
//     if (GET_OPTION(usemldata))
//       printf("New redundant clause created. ID: %d Conflicts: %lu glue: %d\n", c->cl_id, CONFLICTS, c->glue);
  }
  return ref;
}

static void
mark_clause_as_garbage (kissat * solver, clause * c)
{
  assert (!c->garbage);
  LOGCLS (c, "garbage");
  if (!c->redundant) {
    kissat_mark_removed_literals (solver, c->size, c->lits);
  } else {
    if (IS_ML) EXTDATA(c).garbage = true;
  }
  REMOVE_CHECKER_CLAUSE (c);
  DELETE_CLAUSE_FROM_PROOF (c);
  dec_clause (solver, c->redundant);
  c->garbage = true;
}

void
kissat_mark_clause_as_garbage (kissat * solver, clause * c)
{
  assert (!c->garbage);
  mark_clause_as_garbage (solver, c);
  size_t bytes = kissat_actual_bytes_of_clause (c);
  ADD (arena_garbage, bytes);
}

clause *
kissat_delete_clause (kissat * solver, clause * c)
{
  LOGCLS (c, "delete");
  assert (c->size > 2);
  assert (c->garbage);
  size_t bytes = kissat_actual_bytes_of_clause (c);
  SUB (arena_garbage, bytes);
  INC (clauses_deleted);
  return (clause *) ((char *) c + bytes);
}

void
kissat_delete_binary (kissat * solver, bool redundant, unsigned a, unsigned b)
{
  LOGBINARY (a, b, "delete");
  if (!redundant)
    {
      kissat_mark_removed_literal (solver, a);
      kissat_mark_removed_literal (solver, b);
    }
  REMOVE_CHECKER_BINARY (a, b);
  DELETE_BINARY_FROM_PROOF (a, b);
  dec_clause (solver, redundant);
  INC (clauses_deleted);
}

void clause_print_extdata(extdata* d) {
  printf(" cl_id (extdata): %d", d->cl_id);
  printf(" garbage: %d", d->garbage);

  printf(" clause_born: %d", d->clause_born);

    for(int i = 0; i < 2; i ++) {
      printf(" discounted_props_used[0]: %f", d->discounted_props_used[i]);
      printf(" discounted_uip1_used[0]: %f", d->discounted_uip1_used[i]);

    }

    printf(" sum_props_used: %d", d->sum_props_used);
    printf(" sum_uip1_used: %d", d->sum_uip1_used);

    printf(" prop_ranking: %f", d->sum_props_used_per_time_rank_rel);
    printf(" uip1_ranking: %f", d->sum_uip1_used_per_time_rank_rel);
    printf(" last_touched_ranking: %f", d->last_touched_rank_rel);

    for(int i = 0; i < 2; i ++) {
      printf(" pred_lev[%d]: %f", i, d->pred_lev[i]);
    }
}

void clause_print_stats(kissat* solver, clause* c) {
  printf("Clause STATS. cl_id: %d", c->cl_id);
  printf(" size: %d", c->size);
  printf(" shrunken: %d", c->shrunken);
  printf(" redundant: %d", c->redundant);
  printf(" garbage: %d", c->garbage);
  printf(" keep: %d", c->keep);
  if (c->redundant && IS_ML) {
    extdata* d = &EXTDATA(c);
    clause_print_extdata(d);
  }
  printf("\n");
}
