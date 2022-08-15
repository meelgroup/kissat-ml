#ifndef PREDICT_H__
#define PREDICT_H__

#include "xgboost/c_api.h"

#define PRED_COLS 11

struct predict {
    int inited;
    BoosterHandle handle[3];
    DMatrixHandle dmat;
    const float *out_result_short;
};

void predict_setup(struct predict* pred);
void predict_setup2(struct predict* pred, float* const data, const uint32_t num);
void predict_load_models(struct predict* pred,
                         const char* short_fname,
                         const char* medium_fname,
                         const char* long_fname);
void predict_all(struct predict* pred, const uint32_t num, int tier);
double predict_get_at(struct predict* pred, const uint32_t at);
void predict_finish(struct predict* pred);


#endif
