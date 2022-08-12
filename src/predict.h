#ifndef PREDICT_H__
#define PREDICT_H__

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

#include "xgboost/c_api.h"

#define PRED_COLS 11

struct predict {
    BoosterHandle handle;
    DMatrixHandle dmat;
    const float *out_result_short;
};

void predict_setup()

#endif
