#ifndef PREDICT_H__
#define PREDICT_H__

#include "xgboost/c_api.h"

#define PRED_COLS 11

struct predict {
    int inited;
    BoosterHandle handle;
    DMatrixHandle dmat;
    const float *out_result_short;
};

void predict_setup(struct predict* pred);
void predict_load_models(struct predict* pred, const char* short_fname);
void predict_all(struct predict* pred, float* const data, const uint32_t num);
double pred_get_at(struct predict* pred, const uint32_t at);
void predict_finish(struct predict* pred);


#endif
