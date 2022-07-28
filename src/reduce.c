#include "allocate.h"
#include "collect.h"
#include "inline.h"
#include "print.h"
#include "reduce.h"
#include "rank.h"
#include "report.h"
#include "trail.h"
#include "stack.h"

#include <inttypes.h>
#include <math.h>

bool
kissat_reducing (kissat * solver)
{
  if (!GET_OPTION (reduce))
    return false;
  if (!solver->statistics.clauses_redundant)
    return false;
  if (CONFLICTS < solver->limits.reduce.conflicts)
    return false;
  return true;
}

typedef struct reducible reducible;

struct reducible
{
  uint64_t rank;
  unsigned ref; //BEGIN_STACK (solver->arena); + ref == pointer to clause
};

#define RANK_REDUCIBLE(RED) \
  (RED).rank

// *INDENT-OFF*
typedef STACK (reducible) reducibles;
// *INDENT-ON*


int comp_sum_prop_per(const void* a, const void* b) {
  extdata * a1 = (extdata*) a;
  extdata * b1 = (extdata*) b;
  return b1->sum_props_used_per_time > a1->sum_props_used_per_time;
}

int comp_sum_uip1_per(const void* a, const void* b) {
  extdata * a1 = (extdata*) a;
  extdata * b1 = (extdata*) b;
  return b1->sum_uip1_used_per_time > a1->sum_uip1_used_per_time;
}

void dump_ml_data(kissat* solver) {
  assert(IS_ML);
  extdata* begin = BEGIN_STACK(solver->extra_data);
  extdata* end = END_STACK(solver->extra_data);

  //Compacting, so ranking will be correct.
  printf("Compacting...\n");
  extdata* i = begin;
  extdata* j = begin;
  while(i != end) {
    if (!i->garbage  && !i->found) {
        // TODO why do we have these at all?
        printf("Not found but also not garbage."); clause_print_stats(solver, i->cl_ref);
        //i->garbage = true;
        //i->cl_ref->garbage = true;
      }
    if (!i->garbage && i->found) {
      memmove(j, i, sizeof(extdata));
      j->cl_ref->extra_data_idx = j-begin;
      j++;
    }
    i++;
  }
  SET_END_OF_STACK(solver->extra_data, j);

  // Ordering
  printf("Ordering...\n");
  end = END_STACK(solver->extra_data);
  #define RANK_LAST_TOUCHED(RED) (RED).last_touched
  RADIX_STACK (extdata, int, solver->extra_data, RANK_LAST_TOUCHED);
  for(int i = 0, size = SIZE_STACK(solver->extra_data); i < size; i++) {
    PEEK_STACK(solver->extra_data, i).last_touched_rank_rel = (double)i/(double)size;
  }
  printf("Ordered last_touched.\n");

  qsort(BEGIN_STACK(solver->extra_data), SIZE_STACK(solver->extra_data), sizeof(extdata), comp_sum_prop_per);
  for(int i = 0, size = SIZE_STACK(solver->extra_data); i < size; i++) {
    PEEK_STACK(solver->extra_data, i).sum_props_used_per_time_rank_rel = (double)i/(double)size;
  }
  printf("Ordered sum_prop_per.\n");

  qsort(BEGIN_STACK(solver->extra_data), SIZE_STACK(solver->extra_data), sizeof(extdata), comp_sum_uip1_per);
  for(int i = 0, size = SIZE_STACK(solver->extra_data); i < size; i++) {
    PEEK_STACK(solver->extra_data, i).sum_uip1_used_per_time_rank_rel = (double)i/(double)size;
  }
  printf("Ordered sum_uip1_per.\n");

  //fix up clauses' references after sorting
  printf("Fixing up references\n");
  begin = BEGIN_STACK(solver->extra_data);
  end = END_STACK(solver->extra_data);
  i = begin;
  while(i != end) {
    clause* c = i->cl_ref;
    c->extra_data_idx = i-begin;
    assert(c->cl_id == EXTDATA(c).cl_id);
    i++;
  }

  printf("After ranking and all:\n");
  for(extdata* r = BEGIN_STACK(solver->extra_data); r != end; r++) {
    clause_print_stats(solver, r->cl_ref);
    //clause_print_extdata(r);
    printf("\n");
  }
  printf("Finished.\n");

  ward *const arena = BEGIN_STACK (solver->arena);
  const clause *const end_c = (clause *) END_STACK (solver->arena);
  for (clause * c = (clause*)arena; c != end_c; c = kissat_next_clause (c))
    {
      if (c->garbage) continue;
      if (!c->redundant) continue;
      clause_print_stats(solver, c);
      assert(c->cl_id == EXTDATA(c).cl_id);
    }
}

/// Takes reducibles from BEGIN_STACK (solver->arena) and puts them into (reducibles * reds).
static bool
collect_reducibles (kissat * solver, reducibles * reds, reference start_ref)
{
  // Set up found = false for all of extra_data
  extdata* begin_e = BEGIN_STACK(solver->extra_data);
  extdata* end_e = END_STACK(solver->extra_data);
  printf("setting up found=false...\n");
  while(begin_e != end_e) {
    begin_e->found = false;
    begin_e++;
  }


  assert (start_ref != INVALID_REF);
  assert (start_ref <= SIZE_STACK (solver->arena));
  ward *const arena = BEGIN_STACK (solver->arena);
  clause *start = (clause *) (arena + start_ref);
  if (GET_OPTION(genmldata) || GET_OPTION(usemldata)) {
    // must start from zero so we can re-arrange things correctly after sorting.
    assert(start_ref == 0);
  }
  const clause *const end = (clause *) END_STACK (solver->arena);
  assert (start < end);
  while (start != end && (!start->redundant || (!GET_OPTION(genmldata) && start->keep))) {
    start = kissat_next_clause (start);
    if (start != end && GET_OPTION(usemldata))
        assert(!start->keep && "In `usemldata` mode, we don't lock anything in");
  }
  if (start == end)
    {
      solver->first_reducible = INVALID_REF;
      LOG ("no reducible clause candidate left");
      return false;
    }
  const reference redundant = (ward *) start - arena;
#ifdef LOGGING
  if (redundant < solver->first_reducible)
    LOG ("updating redundant clauses start from %zu to %zu",
	 (size_t) solver->first_reducible, (size_t) redundant);
  else
    LOG ("no update to redundant clauses start %zu",
	 (size_t) solver->first_reducible);
#endif
  solver->first_reducible = redundant;
  const unsigned tier2 = GET_OPTION (tier2);
  for (clause * c = start; c != end; c = kissat_next_clause (c))
    {
      if (!c->redundant) {
        if (c->extra_data_idx != -1) {
          //used to be redundant but no longer
          EXTDATA(c).garbage = true;
        }
	continue;
      }
      if (c->garbage) {
        if (IS_ML) EXTDATA(c).garbage = true;
	continue;
      }
      assert (!IS_ML || !EXTDATA(c).garbage);

      if (IS_ML) {
        // now update sums, discounted stuff etc
        const int lifetime = (double)(CONFLICTS-EXTDATA(c).clause_born);
        const int this_round_len = CONFLICTS - solver->limits.last_reduce.conflicts;
        const int cl_this_round_len = this_round_len > lifetime ? this_round_len : lifetime;
        assert(this_round_len > 0);
        assert(cl_this_round_len >= 0);

        extdata* e = &EXTDATA(c);
        e->sum_props_used += c->props_used;
        e->sum_uip1_used += c->uip1_used;

        double until_now_scale = (double)(lifetime-cl_this_round_len)/(double)lifetime;
        double this_round_scale = (double)(cl_this_round_len)/(double)lifetime;

        e->cl_ref = c;
        e->found = true;
        if (lifetime == 0) {
          e->props_used_per_conf = 0;
          e->uip1_used_per_conf = 0;
          e->discounted_props_used[0] = 0;
          e->discounted_props_used[1] = 0;
          e->discounted_uip1_used[0] = 0;
          e->discounted_uip1_used[1] = 0;
          e->sum_props_used_per_time = 0;
          e->sum_uip1_used_per_time = 0;
        } else {
          assert(cl_this_round_len > 0);
          assert(lifetime > 0);

          const double rate = 0.7;
          e->discounted_props_used[0] =
            e->discounted_props_used[0]*rate*until_now_scale + c->props_used*(1.0-rate)*this_round_scale;
          e->discounted_props_used[1] =
            e->discounted_props_used[1]*(1.0-rate)*until_now_scale + c->props_used*rate*this_round_scale;
          e->discounted_uip1_used[0] =
            e->discounted_uip1_used[0]*rate*until_now_scale + c->uip1_used*(1.0-rate)*this_round_scale;
          e->discounted_uip1_used[1] =
            e->discounted_uip1_used[1]*(1.0-rate)*until_now_scale + c->uip1_used*rate*this_round_scale;

          e->props_used_per_conf = (double)c->props_used/(double)cl_this_round_len;
          e->uip1_used_per_conf = (double)c->uip1_used/(double)cl_this_round_len;
          //printf("e->cl_id: %d c->cl_id: %d\n", e->cl_id, c->cl_id);
          assert(e->cl_id == c->cl_id);

          e->sum_props_used_per_time = (double)e->sum_props_used/(double)lifetime;
          e->sum_uip1_used_per_time = (double)e->sum_uip1_used/(double)lifetime;
        }
        e->last_touched = c->last_touched;
      }

      if (c->reason)
	goto end;

      if (!GET_OPTION(usemldata)) {
        if (c->keep)
          goto end;
      }
      if (c->used)
      {
        c->used--;
        if (!GET_OPTION (genmldata) &&
          (c->glue <= tier2))
          goto end;
      }
      assert (!c->garbage);
      assert (kissat_clause_in_arena (solver, c));
      reducible red;
      const uint64_t negative_size = ~c->size;
      const uint64_t negative_glue = ~c->glue;
      red.rank = negative_size | (negative_glue << 32); // 1st tie break: glue, 2nd tie break: size.
      red.ref = (ward *) c - arena;
      PUSH_STACK (*reds, red);

      end:
      // Zero things out
      c->props_used = 0;
      c->uip1_used = 0;
    }
  if (EMPTY_STACK (*reds))
    {
      LOG ("did not find any reducible redundant clause");
      return false;
    }
  return true;
}

#define USEFULNESS RANK_REDUCIBLE

static void
sort_reducibles (kissat * solver, reducibles * reds)
{
  RADIX_STACK (reducible, uint64_t, *reds, USEFULNESS);
}

static void
mark_less_useful_clauses_as_garbage (kissat * solver, reducibles * reds)
{
  const size_t size = SIZE_STACK (*reds);
  size_t target = size * (GET_OPTION (reducefraction) / 100.0);
#ifndef QUIET
  statistics *statistics = &solver->statistics;
  const size_t clauses =
    statistics->clauses_irredundant + statistics->clauses_redundant;
  kissat_phase (solver, "reduce",
		GET (reductions),
		"reducing %zu (%.0f%%) out of %zu (%.0f%%) "
		"reducible clauses",
		target, kissat_percent (target, size),
		size, kissat_percent (size, clauses));
#endif
  unsigned reduced = 0;
  ward *arena = BEGIN_STACK (solver->arena);
  const reducible *const begin = BEGIN_STACK (*reds);
  const reducible *const end = END_STACK (*reds);
  for (const reducible * p = begin; p != end && target--; p++)
    {
      clause *c = (clause *) (arena + p->ref);
      assert (kissat_clause_in_arena (solver, c));
      assert (!c->garbage);
      assert (GET_OPTION(usemldata) || !c->keep);
      assert (!c->reason);
      assert (c->redundant);
      LOGCLS (c, "reducing");
      kissat_mark_clause_as_garbage (solver, c);
      reduced++;
    }
  ADD (clauses_reduced, reduced);
}

static bool
compacting (kissat * solver)
{
  if (!GET_OPTION (compact))
    return false;
  unsigned inactive = solver->vars - solver->active;
  unsigned limit = GET_OPTION (compactlim) / 1e2 * solver->vars;
  bool compact = (inactive > limit);
  LOG ("%u inactive variables %.0f%% <= limit %u %.0f%%",
       inactive, kissat_percent (inactive, solver->vars),
       limit, kissat_percent (limit, solver->vars));
  return compact;
}

int
kissat_reduce (kissat * solver)
{
  START (reduce);
  printf("solver->statistics.reductions: %ld\n", solver->statistics.reductions);
  INC (reductions);
  kissat_phase (solver, "reduce", GET (reductions),
		"reduce limit %" PRIu64 " hit after %" PRIu64
		" conflicts", solver->limits.reduce.conflicts, CONFLICTS);
  bool compact = compacting (solver);
  reference start = compact ? 0 : solver->first_reducible;
  if (IS_ML) start = 0;
  if (start != INVALID_REF)
    {
#ifndef QUIET
      size_t arena_size = SIZE_STACK (solver->arena);
      size_t words_to_sweep = arena_size - start;
      size_t bytes_to_sweep = sizeof (word) * words_to_sweep;
      kissat_phase (solver, "reduce", GET (reductions),
		    "reducing clauses after offset %"
		    REFERENCE_FORMAT " in arena", start);
      kissat_phase (solver, "reduce", GET (reductions),
		    "reducing %zu words %s %.0f%%",
		    words_to_sweep, FORMAT_BYTES (bytes_to_sweep),
		    kissat_percent (words_to_sweep, arena_size));
#endif
      if (kissat_flush_and_mark_reason_clauses (solver, start))
	{
	  reducibles reds;
	  INIT_STACK (reds);
	  if (collect_reducibles (solver, &reds, start))
	    {
              assert(!(GET_OPTION(usemldata) && GET_OPTION(usemldata)));
              if (GET_OPTION(genmldata)) dump_ml_data(solver);
              if (GET_OPTION(usemldata)) {
                assert(false && "TODO");
              } else {
                sort_reducibles (solver, &reds);
                mark_less_useful_clauses_as_garbage (solver, &reds);
              }
              RELEASE_STACK (reds);
              kissat_sparse_collect (solver, compact, start);
	    }
	  else if (compact)
	    kissat_sparse_collect (solver, compact, start);
	  else
	    kissat_unmark_reason_clauses (solver, start);
	}
      else
	assert (solver->inconsistent);
    }
  else
    kissat_phase (solver, "reduce", GET (reductions), "nothing to reduce");
  UPDATE_CONFLICT_LIMIT (reduce, reductions, SQRT, false);
  REPORT (0, '-');
  solver->limits.last_reduce.conflicts = CONFLICTS;
  STOP (reduce);
  return solver->inconsistent ? 20 : 0;
}
