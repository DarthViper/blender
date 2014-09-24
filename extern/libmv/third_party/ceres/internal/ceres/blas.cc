// Ceres Solver - A fast non-linear least squares minimizer
// Copyright 2013 Google Inc. All rights reserved.
// http://code.google.com/p/ceres-solver/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// * Neither the name of Google Inc. nor the names of its contributors may be
//   used to endorse or promote products derived from this software without
//   specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Author: sameeragarwal@google.com (Sameer Agarwal)

#include "ceres/blas.h"
#include "ceres/internal/port.h"
#include "glog/logging.h"

extern "C" void dsyrk_(char* uplo,
                       char* trans,
                       int* n,
                       int* k,
                       double* alpha,
                       double* a,
                       int* lda,
                       double* beta,
                       double* c,
                       int* ldc);

namespace ceres {
namespace internal {

void BLAS::SymmetricRankKUpdate(int num_rows,
                                int num_cols,
                                const double* a,
                                bool transpose,
                                double alpha,
                                double beta,
                                double* c) {
#ifdef CERES_NO_LAPACK
  LOG(FATAL) << "Ceres was built without a BLAS library.";
#else
  char uplo = 'L';
  char trans = transpose ? 'T' : 'N';
  int n = transpose ? num_cols : num_rows;
  int k = transpose ? num_rows : num_cols;
  int lda = k;
  int ldc = n;
  dsyrk_(&uplo,
         &trans,
         &n,
         &k,
         &alpha,
         const_cast<double*>(a),
         &lda,
         &beta,
         c,
         &ldc);
#endif
}

}  // namespace internal
}  // namespace ceres
