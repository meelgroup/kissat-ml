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


#define safe_xgboost(call) {  \
  int err = (call); \
  if (err != 0) { \
    fprintf(stderr, "%s:%d: error in %s: %s\n", __FILE__, __LINE__, #call, XGBGetLastError());  \
    exit(1); \
  } \
}

using namespace CMSat;

void predict_setup(struct predict* pred)
{
    safe_xgboost(XGBoosterCreate(0, 0, &(pred->handle)))
    safe_xgboost(XGBoosterSetParam(pred->handle, "nthread", "1"))
}

void predict_del(struct predict* pred)
{
    XGBoosterFree(pred->handle);
}

void predict_load_models(struct predict* pred, const char* short_fname)
{
    safe_xgboost(XGBoosterLoadModel(pred->handle));
}

void predict_all(struct predict* pred, float* const data, const uint32_t num)
{
    safe_xgboost(
        XGDMatrixCreateFromMat(data, num, PRED_COLS, nanf(""), &pred->dmat));

    bst_ulong out_len;
    safe_xgboost(XGBoosterPredict(
        pred->handle,
        pred->dmat,
        0,  //0: normal prediction
        0,  //use all trees
        0,  //do not use for training
        &out_len,
        &pred->out_result_short
    ))
    assert(out_len == num);
}

double pred_get_at(struct predict* pred, const uint32_t at)
{
    return pred->out_result_short[at];
}

void predict_finish(struct predict* pred)
{
    safe_xgboost(XGDMatrixFree(pred->dmat));
}
