// Copyright (c) 2012-2013, the Scal Project Authors.  All rights reserved.
// Please see the AUTHORS file for details.  Use of this source code is governed
// by a BSD license that can be found in the LICENSE file.

#include <gflags/gflags.h>

#include "benchmark/std_glue/std_pipe_api.h"
#include "datastructures/ms_queue.h"

void* ds_new(void) {
  MSQueue<uint64_t> *msq = new MSQueue<uint64_t>();
  return static_cast<void*>(msq);
}

char* ds_get_stats(void) {
  return NULL;
}
