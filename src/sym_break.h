#ifndef _sym_break_hpp_INCLUDED
#define _sym_break_hpp_INCLUDED

#include <stdbool.h>

struct kissat;
struct clause;

bool kissat_sym_breaking (struct kissat *);
int kissat_sym_break (struct kissat *);

#endif
