/*
 * H.265 video codec.
 * Copyright (c) 2013-2014 struktur AG, Dirk Farin <farin@struktur.de>
 *
 * Authors: struktur AG, Dirk Farin <farin@struktur.de>
 *
 * This file is part of libde265.
 *
 * libde265 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * libde265 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libde265.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "libde265/encoder/algo/cb-split.h"
#include "libde265/encoder/encoder-context.h"
#include <assert.h>
#include <limits>
#include <math.h>


#define ENCODER_DEVELOPMENT 1



bool Algo_CB_Split::forcedSplit(const de265_image* input, int x0,int y0, int Log2CbSize) const
{
  int w = input->get_width();
  int h = input->get_height();
  int cbSize = (1<<Log2CbSize);

  if (x0+cbSize > w) return true;
  if (y0+cbSize > h) return true;
  return false;
}


// Utility function to encode all four children in a splitted CB.
// Children are coded with the specified algo_cb_split.
enc_cb* Algo_CB_Split::encode_cb_split(encoder_context* ectx,
                                       context_model_table ctxModel,
                                       const de265_image* input,
                                       int x0,int y0, int Log2CbSize, int ctDepth)
{
  int w = input->get_width();
  int h = input->get_height();


  // create a splitted CB node
  enc_cb* cb = new enc_cb();

  cb->split_cu_flag = true;

  cb->cu_transquant_bypass_flag = false;
  cb->log2CbSize = Log2CbSize;
  cb->ctDepth = ctDepth;

  // rate for split_cu_flag (=true)

  CABAC_encoder_estim estim;
  ectx->switch_CABAC(ctxModel, &estim);
  encode_quadtree(ectx,cb,x0,y0,Log2CbSize,ctDepth, false);

  cb->distortion = 0;
  cb->rate       = estim.getRDBits();


  // encode all 4 children and sum their distortions and rates

  for (int i=0;i<4;i++) {
    int dx = (i&1)  << (Log2CbSize-1);
    int dy = (i>>1) << (Log2CbSize-1);

    if (x0+dx>=w || y0+dy>=h) {
      cb->children[i] = NULL;
    }
    else {
      cb->children[i] = analyze(ectx, ctxModel,
                                input, x0+dx, y0+dy,
                                Log2CbSize-1, ctDepth+1);

      cb->distortion += cb->children[i]->distortion;
      cb->rate       += cb->children[i]->rate;
    }
  }

  return cb;
}




enc_cb* Algo_CB_Split_BruteForce::analyze(encoder_context* ectx,
                                          context_model_table ctxModel,
                                          const de265_image* input,
                                          int x0,int y0, int Log2CbSize,
                                          int ctDepth)
{
  // if we try both variants, make a copy of the ctxModel and use the copy for splitting

  const bool can_split_CB   = (Log2CbSize > ectx->sps.Log2MinCbSizeY);
  const bool can_nosplit_CB = !forcedSplit(input,x0,y0,Log2CbSize);

  context_model_table ctxCopy;
  context_model* ctxSplit = ctxModel;

  if (can_split_CB && can_nosplit_CB) {
    copy_context_model_table(ctxCopy, ctxModel);
    ctxSplit=ctxCopy;
  }


  // try encoding without splitting

  enc_cb* cb_no_split = NULL;
  enc_cb* cb_split    = NULL;

  if (can_nosplit_CB) {
    cb_no_split = mIntraPartModeAlgo->analyze(ectx, ctxModel, input,
                                              x0,y0, Log2CbSize, ctDepth);
  }

  // if possible, try to split CB

  if (can_split_CB) {
    cb_split = encode_cb_split(ectx, ctxSplit,
                               input,x0,y0, Log2CbSize, ctDepth);
  }


  // if only one variant has been tested, choose this

  if (!can_nosplit_CB) { return cb_split;    }
  if (!can_split_CB)   { return cb_no_split; }


  // compute RD costs for both variants

  const float rd_cost_split    = cb_split->distortion    + ectx->lambda * cb_split->rate;
  const float rd_cost_no_split = cb_no_split->distortion + ectx->lambda * cb_no_split->rate;

  const bool split_is_better =  (rd_cost_split < rd_cost_no_split);

  if (split_is_better) {
    copy_context_model_table(ctxModel, ctxCopy);
    delete cb_no_split;
    return cb_split;
  }
  else {
    // have to reconstruct state of the first option

    cb_no_split->write_to_image(ectx->img, x0,y0, true);
    cb_no_split->reconstruct(&ectx->accel, ectx->img, x0,y0);
    delete cb_split;
    return cb_no_split;
  }
}

