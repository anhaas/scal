// Copyright (c) 2012-2013, the Scal Project Authors.  All rights reserved.
// Please see the AUTHORS file for details.  Use of this source code is governed
// by a BSD license that can be found in the LICENSE file.

#include <gflags/gflags.h>

#include "benchmark/std_glue/std_pipe_api.h"
#include "datastructures/ts_timestamp.h"
#include "datastructures/ts_stack.h"
#include "datastructures/ts_deque.h"

DEFINE_bool(array, false, "use the array-based inner buffer");
DEFINE_bool(list, false, "use the linked-list-based inner buffer");
DEFINE_bool(2ts, false, "use the 2-time-stamp inner buffer");
DEFINE_bool(stutter_clock, false, "use the stuttering clock");
DEFINE_bool(atomic_clock, false, "use atomic fetch-and-inc clock");
DEFINE_bool(hw_clock, false, "use the RDTSC hardware clock");
DEFINE_bool(hwp_clock, false, "use the RDTSCP hardware clock");
DEFINE_bool(init_threshold, true, "initializes the dequeue threshold "
    "with the current time");
DEFINE_uint64(delay, 0, "delay in the insert operation");

// TL2TSStackBuffer<uint64_t> *ts_;
TSStack<uint64_t, TL2TSStackBuffer<uint64_t, HardwarePTimeStamp>
  , HardwarePTimeStamp> *ts_;

void* ds_new() {
//   TimeStamp *timestamping;
//   if (FLAGS_stutter_clock) {
//     timestamping = new StutteringTimeStamp(g_num_threads + 1);
//   } else if (FLAGS_atomic_clock) {
//     timestamping = new AtomicCounterTimeStamp();
//   } else if (FLAGS_hw_clock) {
//     timestamping = new HardwareTimeStamp();
//   } else if (FLAGS_hwp_clock) {
//     timestamping = new HardwarePTimeStamp();
//   } else {
//     timestamping = new HardwareTimeStamp();
//   }
//   TSStackBuffer<uint64_t> *buffer;
//   if (FLAGS_array) {
//     buffer = new TLArrayStackBuffer<uint64_t>(g_num_threads + 1);
//   } else if (FLAGS_list) {
//     buffer = new TLLinkedListStackBuffer<uint64_t>(g_num_threads + 1);
//   } else if (FLAGS_2ts) {
//     buffer 
//       = new TL2TSStackBuffer<uint64_t>(g_num_threads + 1, FLAGS_delay);
//   } else {
//     buffer 
//       = new TL2TSStackBuffer<uint64_t>(g_num_threads + 1, FLAGS_delay);
//   }
//   ts_ = new TSStack<uint64_t>
//         (buffer, timestamping, FLAGS_init_threshold, g_num_threads + 1);
//   ts_ = new TSStack<uint64_t>(
//       new TL2TSStackBuffer<uint64_t>(g_num_threads + 1, FLAGS_delay),
//       new HardwarePTimeStamp(),
//       true, g_num_threads + 1);
//   return static_cast<void*>(ts_);
  ts_ =  new TSStack<uint64_t, 
      TL2TSStackBuffer<uint64_t, HardwarePTimeStamp>, HardwarePTimeStamp>
        (g_num_threads + 1, FLAGS_delay);
  return ts_;
  return new TSStack<uint64_t, 
      TL2TSStackBuffer<uint64_t, HardwarePTimeStamp>, HardwarePTimeStamp>
        (g_num_threads + 1, FLAGS_delay);
  return new TSStack<uint64_t, TL2TSDequeBuffer<uint64_t, HardwarePTimeStamp>, HardwarePTimeStamp>
    (g_num_threads + 1, FLAGS_delay);
}

char* ds_get_stats(void) {
  return ts_->ds_get_stats();
//  return NULL;
}
