/******************************************
Copyright (C) 2009-2020 Authors of CryptoMiniSat, see AUTHORS file

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
***********************************************/

#include "predict.h"
#include "assert.h"
#include "stdlib.h"


#define safe_xgboost(call) {  \
  int err = (call); \
  if (err != 0) { \
    fprintf(stderr, "%s:%d: error in %s: %s\n", __FILE__, __LINE__, #call, XGBGetLastError());  \
    exit(1); \
  } \
}

void predict_setup(struct predict* pred)
{
    if (pred->inited == 0) {
        printf("Initing xgboost...\n");
        for(int i = 0; i < 3; i++) {
            safe_xgboost(XGBoosterCreate(0, 0, &(pred->handle[i])))
            safe_xgboost(XGBoosterSetParam(pred->handle[i], "nthread", "1"))
        }
        pred->inited = 1;
    }
}

void predict_del(struct predict* pred)
{
    XGBoosterFree(pred->handle);
}

void predict_load_models(struct predict* pred,
                         const char* short_fname,
                         const char* medium_fname,
                         const char* long_fname)
{
    assert(pred->inited > 0);
    if (pred->inited == 1) {
        printf("Loading model...\n");
        safe_xgboost(XGBoosterLoadModel(pred->handle[0], short_fname))
        safe_xgboost(XGBoosterLoadModel(pred->handle[1], medium_fname))
        safe_xgboost(XGBoosterLoadModel(pred->handle[2], long_fname))
        pred->inited = 2;
    }
}

void predict_setup2(struct predict* pred, float* const data, const uint32_t num) {
    safe_xgboost(
        XGDMatrixCreateFromMat(data, num, PRED_COLS, -1, &pred->dmat));
}

void predict_all(struct predict* pred, uint32_t num, int tier)
{
    bst_ulong out_len;
    safe_xgboost(XGBoosterPredict(
        pred->handle[tier],
        pred->dmat,
        0,  //0: normal prediction
        0,  //use all trees
        0,  //do not use for training
        &out_len,
        &pred->out_result_short
    ))
    assert(out_len == num);
}

double predict_get_at(struct predict* pred, const uint32_t at)
{
    return pred->out_result_short[at];
}

void predict_finish(struct predict* pred)
{
    safe_xgboost(XGDMatrixFree(pred->dmat))
}
