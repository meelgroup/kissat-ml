#include "allocate.h"
#include "collect.h"
#include "inline.h"
#include "print.h"
#include "reduce.h"
#include "rank.h"
#include "report.h"
#include "trail.h"
#include "stack.h"
#include "predict.h"

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
  clause* c;
  extdata* e;
  unsigned ref; //BEGIN_STACK (solver->arena); + ref == pointer to clause
};

#define RANK_REDUCIBLE(RED) \
  (RED).rank

// *INDENT-OFF*
typedef STACK (reducible) reducibles;
// *INDENT-ON*


int comp_sum_prop_per(const void* a, const void* b) {
  reducible * a1 = (reducible*) a;
  reducible * b1 = (reducible*) b;
  return b1->e->sum_props_used_per_time > a1->e->sum_props_used_per_time;
}

int comp_prop(const void* a, const void* b) {
  reducible * a1 = (reducible*) a;
  reducible * b1 = (reducible*) b;
  return b1->c->props_used > a1->c->props_used;
}

int comp_uip1(const void* a, const void* b) {
  reducible * a1 = (reducible*) a;
  reducible * b1 = (reducible*) b;
  return b1->c->uip1_used > a1->c->uip1_used;
}

int comp_pred(const void* a, const void* b) {
  reducible * a1 = (reducible*) a;
  reducible * b1 = (reducible*) b;
  return b1->e->pred_lev[0] > a1->e->pred_lev[0];
}


int comp_sum_uip1_per(const void* a, const void* b) {
  reducible * a1 = (reducible*) a;
  reducible * b1 = (reducible*) b;
  return b1->e->sum_uip1_used_per_time > a1->e->sum_uip1_used_per_time;
}

void sort_ml(kissat* solver, reducibles* reds, int tier) {
  assert(IS_ML);
  reducible* end = END_STACK(*reds);

  // Ordering
  kissat_verbose(solver, "Ordering...\n");

  qsort(BEGIN_STACK(*reds), SIZE_STACK(*reds), sizeof(reducible), comp_sum_prop_per);
  for(unsigned i = 0, size = SIZE_STACK(*reds); i < size; i++) {
    PEEK_STACK(*reds, i).e->sum_props_used_per_time_rank_rel = (double)i/(double)size;
  }
  kissat_verbose(solver, "Ordered sum_prop_per.\n");

  qsort(BEGIN_STACK(*reds), SIZE_STACK(*reds), sizeof(reducible), comp_sum_uip1_per);
  for(unsigned i = 0, size = SIZE_STACK(*reds); i < size; i++) {
    PEEK_STACK(*reds, i).e->sum_uip1_used_per_time_rank_rel = (double)i/(double)size;
  }
  kissat_verbose(solver, "Ordered sum_uip1_per.\n");

  qsort(BEGIN_STACK(*reds), SIZE_STACK(*reds), sizeof(reducible), comp_prop);
  for(unsigned i = 0, size = SIZE_STACK(*reds); i < size; i++) {
    PEEK_STACK(*reds, i).e->props_ranking_rel = (double)i/(double)size;
  }
  kissat_verbose(solver, "Ordered prop.\n");

  qsort(BEGIN_STACK(*reds), SIZE_STACK(*reds), sizeof(reducible), comp_uip1);
  for(unsigned i = 0, size = SIZE_STACK(*reds); i < size; i++) {
    PEEK_STACK(*reds, i).e->uip1_ranking_rel = (double)i/(double)size;
  }
  kissat_verbose(solver, "Ordered uip1.\n");

  predict_setup(&solver->pred);
  predict_load_models(&solver->pred,
                      "short_anc.json",
                      "medium_anc.json",
                      "long.json");

//   printf("After ranking and all:\n");
  float* data = malloc(sizeof(float)*PRED_COLS*SIZE_STACK(*reds));
  float* d = data;
  for(reducible* r = BEGIN_STACK(*reds); r != end; r++) {
//     clause_print_stats(solver, r->cl_ref);
//     //clause_print_extdata(r);
//     printf("\n");
    *d++ = r->e->sum_uip1_used_per_time_rank_rel;
    *d++ = r->e->sum_props_used_per_time_rank_rel;
    *d++ = r->e->uip1_ranking_rel;
    *d++ = r->e->props_ranking_rel;
    *d++ = r->c->props_used;
    *d++ = r->c->uip1_used;
    *d++ = r->e->discounted_props_used[0];
    *d++ = r->e->discounted_props_used[1];
    *d++ = r->e->discounted_uip1_used[0];
    *d++ = r->e->discounted_uip1_used[1];
    *d++ = r->c->glue;
    *d++ = (int)CONFLICTS-(int)r->c->last_touched;
  }
//   printf("Finished.\n");

  predict_setup2(&solver->pred, data, SIZE_STACK(*reds));
  predict_all(&solver->pred, SIZE_STACK(*reds), tier);

  int at = 0;
  for(reducible* r = BEGIN_STACK(*reds); r != end; r++) {
    r->e->pred_lev[0] = predict_get_at(&solver->pred, at);
    at++;
  }
  free(data);

  qsort(BEGIN_STACK(*reds), SIZE_STACK(*reds), sizeof(reducible), comp_pred);
  end = END_STACK(*reds);

  int num_to_del = (int)SIZE_STACK(*reds)-GET_OPTION(mlsize);
  at = 0;
  for(int i = SIZE_STACK(*reds)-1; i >= 0 && num_to_del > 0; i--, num_to_del--) {
    reducible* r = &PEEK_STACK(*reds, i);
    if (GET_OPTION(verbose)) {
      clause_print_stats(solver, r->c);
      clause_print_extdata(r->e);
      printf(" -> predval: %f", r->e->pred_lev[0]);
    }

    const int lifetime = (int)CONFLICTS-(int)r->e->clause_born;
    assert(r->c->garbage == 0);
    assert(r->c->redundant == 1);

    if (num_to_del > 0 && lifetime > 10000) {
      kissat_mark_clause_as_garbage (solver, r->c);
      if (GET_OPTION(verbose)) printf("--> GARBAGE\n");
    } else {
      assert(r->e->tier == -1);
      if (GET_OPTION(verbose)) printf("--> KEEP\n");
    }
    at++;
  }
  kissat_verbose(solver, "----------predval -- FINISH -------------------\n");

  // Just checking below
//   ward *const arena = BEGIN_STACK (solver->arena);
//   const clause *const end_c = (clause *) END_STACK (solver->arena);
//   for (clause * c = (clause*)arena; c != end_c; c = kissat_next_clause (c))
//     {
//       if (c->garbage) continue;
//       if (!c->redundant) continue;
//       //clause_print_stats(solver, c);
//       assert(c->cl_id == EXTDATA(c).cl_id);
//     }
}

/// Takes reducibles from BEGIN_STACK (solver->arena) and puts them into (reducibles * reds).
static bool
collect_reducibles (kissat * solver, reducibles * reds, reference start_ref)
{
  assert (start_ref != INVALID_REF);
  assert (start_ref <= SIZE_STACK (solver->arena));
  ward *const arena = BEGIN_STACK (solver->arena);
  clause *start = (clause *) (arena + start_ref);
  if (GET_OPTION(usemldata)) {
    // must start from zero so we can re-arrange things correctly after sorting.
    assert(start_ref == 0);
  }
  const clause *const end = (clause *) END_STACK (solver->arena);
  assert (start < end);
  while (start != end && (!start->redundant || start->keep)) {
    start = kissat_next_clause (start);
    if (start != end && GET_OPTION(usemldata) && start->redundant)
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

      if (IS_ML) {
        assert(!EXTDATA(c).garbage);
        // now update sums, discounted stuff etc
        const int lifetime = (int)CONFLICTS-(int)EXTDATA(c).clause_born;
        extdata* e = &EXTDATA(c);
        e->sum_props_used += c->props_used;
        e->sum_uip1_used += c->uip1_used;

        //double until_now_scale = (double)(lifetime-cl_this_round_len)/(double)lifetime;
        //double this_round_scale = (double)(cl_this_round_len)/(double)lifetime;
        double until_now_scale = 1.0;
        double this_round_scale = 1.0;

        e->cl_ref = c;
        if (lifetime == 0) {
          e->discounted_props_used[0] = 0;
          e->discounted_props_used[1] = 0;
          e->discounted_uip1_used[0] = 0;
          e->discounted_uip1_used[1] = 0;
          e->sum_props_used_per_time = 0;
          e->sum_uip1_used_per_time = 0;
        } else {
          assert(lifetime > 0);

          const double rate = 0.8;
          const double rate2 = 0.4;
          e->discounted_props_used[0] =
            e->discounted_props_used[0]*rate*until_now_scale + c->props_used*(1.0-rate)*this_round_scale;
          e->discounted_props_used[1] =
            e->discounted_props_used[1]*rate2*until_now_scale + c->props_used*(1.0-rate2)*this_round_scale;
          e->discounted_uip1_used[0] =
            e->discounted_uip1_used[0]*rate*until_now_scale + c->uip1_used*(1.0-rate)*this_round_scale;
          e->discounted_uip1_used[1] =
            e->discounted_uip1_used[1]*rate2*until_now_scale + c->uip1_used*(1.0-rate2)*this_round_scale;

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

        if (c->used)
        {
          c->used--;
          if (c->glue <= tier2)
            goto end;
        }
      }
      assert (!c->garbage);
      assert (kissat_clause_in_arena (solver, c));
      reducible red;
      const uint64_t negative_size = ~c->size;
      const uint64_t negative_glue = ~c->glue;
      red.rank = negative_size | (negative_glue << 32); // 1st tie break: glue, 2nd tie break: size.
      red.ref = (ward *) c - arena;
      red.c = c;
      if (GET_OPTION(usemldata)) red.e = &EXTDATA(c);

      const int tier = EXTDATA(c).tier;
      if (tier == -1 || tier == 0) PUSH_STACK (reds[0], red);
//       else if (tier == 1) PUSH_STACK (reds[1], red);
//       else if (tier == 2) PUSH_STACK (reds[2], red);
//       else assert(false);

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
	  reducibles reds[3];
          for(int i = 0; i < 3; i++) INIT_STACK (reds[i]);
	  if (collect_reducibles (solver, reds, start))
	    {
              if (GET_OPTION(usemldata)) {
                //for(int i = 0; i < 3; i++) sort_ml (solver, &reds[i], i);
                // use MEDIUM_ANC by default
                // use SHORT_ANC by default?
                sort_ml (solver, &reds[0], 0);
              } else {
                sort_reducibles (solver, &reds[0]);
                mark_less_useful_clauses_as_garbage (solver, &reds[0]);
              }
              for(int i = 0; i < 3; i++) RELEASE_STACK (reds[i]);
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
  if (GET_OPTION(usemldata)) {
    solver->statistics.reductionsML++;;
    solver->options.reduceint = 1e4;
    UPDATE_CONFLICT_LIMIT (reduce, reductionsML, UNIT_FUNC, false);
  } else {
    UPDATE_CONFLICT_LIMIT (reduce, reductions, SQRT, false);
  }
  REPORT (0, '-');
  solver->limits.last_reduce.conflicts = CONFLICTS;
  STOP (reduce);
  return solver->inconsistent ? 20 : 0;
}
