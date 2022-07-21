#ifndef _clause_h_INCLUDED
#define _clause_h_INCLUDED

#include "arena.h"
#include "literal.h"
#include "reference.h"
#include "utilities.h"

#include <stdbool.h>

struct extdata {
  bool garbage;
  int clause_born;

  float discounted_props_used[2];
  float discounted_uip1_used[2];

  // Over the whole lifetime of the clause
  int sum_props_used;
  int sum_uip1_used;

  // Calculated
  float sum_props_used_per_time;
  float sum_uip1_used_per_time;

  // Rankings (ordered, 0 is best)
  float sum_props_used_per_time_rank_rel;
  float sum_uip1_used_per_time_rank_rel;
  float last_touched_rank_rel;

  // Last round data / last round length
  float props_used_per_conf;
  float uip1_used_per_conf;
  float last_touched;

  //redundant info, but useful to have here as well, for ML
  int cl_id;
  struct clause* cl_ref; // needed to re-set after qsort

  // ML prediction value
  float pred_lev[2];
};
typedef struct extdata extdata;

typedef struct clause clause;

#define EXTDATA(c) PEEK_STACK(solver->extra_data, c->extra_data_idx)

#define LD_MAX_GLUE 22u
#define MAX_GLUE ((1u<<LD_MAX_GLUE)-1)

struct clause
{
  unsigned glue:LD_MAX_GLUE;

  bool garbage:1;
  bool keep:1; // when set, it's Tier 1 (i.e. never delete)
  bool reason:1;
  bool redundant:1;
  bool shrunken:1; // orig size is not kept. Insead, this is set, and INVALID_LIT is placed at the last point in lits[]
  bool subsume:1;
  bool sweeped:1;
  bool vivify:1;

  unsigned used:2;
  int extra_data_idx;
  int cl_id;
  int props_used;
  int uip1_used;
  int last_touched;

  unsigned searched;
  unsigned size;

  unsigned lits[3];
};

#define SIZE_OF_CLAUSE_HEADER (sizeof(struct clause)-3*sizeof(unsigned))

#define BEGIN_LITS(C) ((C)->lits)
#define END_LITS(C) (BEGIN_LITS (C) + (C)->size)

#define all_literals_in_clause(LIT,C) \
  unsigned LIT, * LIT ## _PTR = BEGIN_LITS (C), \
                * const LIT ## _END = END_LITS (C); \
  LIT ## _PTR != LIT ## _END && ((LIT = *LIT ## _PTR), true); \
  ++LIT ## _PTR

#define all_literals_in_clause_sz(LIT,C, sz) \
  unsigned LIT, * LIT ## _PTR = BEGIN_LITS (C), \
                * const LIT ## _END = (BEGIN_LITS (C) + sz); \
  LIT ## _PTR != LIT ## _END && ((LIT = *LIT ## _PTR), true); \
  ++LIT ## _PTR

static inline size_t
kissat_bytes_of_clause (unsigned size)
{
  const size_t res = sizeof (clause) + (size - 3) * sizeof (unsigned);
  return kissat_align_ward (res);
}

static inline size_t
kissat_actual_bytes_of_clause (clause * c)
{
  unsigned const *p = END_LITS (c);
  if (c->shrunken)
    while (*p++ != INVALID_LIT)
      ;
  return kissat_align_ward ((char *) p - (char *) c);
}

static inline clause *
kissat_next_clause (clause * c)
{
  word bytes = kissat_actual_bytes_of_clause (c);
  return (clause *) ((char *) c + bytes);
}

struct kissat;

void kissat_new_binary_clause (struct kissat *,
			       bool redundant, unsigned, unsigned);

reference kissat_new_original_clause (struct kissat *);
reference kissat_new_irredundant_clause (struct kissat *);
reference kissat_new_redundant_clause (struct kissat *, unsigned glue);

#ifndef INLINE_SORT
void kissat_sort_literals (struct kissat *, unsigned size, unsigned *lits);
#endif

void kissat_connect_clause (struct kissat *, clause *);

clause *kissat_delete_clause (struct kissat *, clause *);
void kissat_delete_binary (struct kissat *, bool redundant, unsigned,
			   unsigned);

void kissat_mark_clause_as_garbage (struct kissat *, clause *);
void clause_print_stats(struct kissat* solver, clause* c);
void clause_print_extdata(extdata* d);

#endif
