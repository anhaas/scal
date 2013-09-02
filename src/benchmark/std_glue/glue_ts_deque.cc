// Copyright (c) 2012-2013, the Scal Project Authors.  All rights reserved.
// Please see the AUTHORS file for details.  Use of this source code is governed
// by a BSD license that can be found in the LICENSE file.

#define __STDC_FORMAT_MACROS 1  // we want PRIu64 and friends

#include <gflags/gflags.h>
#include "benchmark/std_glue/std_pipe_api.h"
#include "datastructures/ts_timestamp.h"
#include "datastructures/ts_deque.h"

DEFINE_bool(list, false, "use the linked-list-based inner buffer");
DEFINE_bool(2ts, false, "use the 2-time-stamp inner buffer");
DEFINE_bool(stutter_clock, false, "use the stuttering clock");
DEFINE_bool(atomic_clock, false, "use atomic fetch-and-inc clock");
DEFINE_bool(hw_clock, false, "use the RDTSC hardware clock");
DEFINE_bool(init_threshold, false, "initializes the dequeue threshold "
    "with the current time");
DEFINE_int64(delay, 0, "delay in the insert operation");

uint64_t g_delay;

void* ds_new() {

  TimeStamp *timestamping;
  if (FLAGS_stutter_clock) {
    timestamping = new StutteringTimeStamp(g_num_threads + 1);
  } else if (FLAGS_atomic_clock) {
    timestamping = new AtomicCounterTimeStamp();
  } else if (FLAGS_hw_clock) {
    timestamping = new ShiftedHardwareTimeStamp();
  } else {
    timestamping = new ShiftedHardwareTimeStamp();
  }

  if (FLAGS_delay >= 0) {
    g_delay = FLAGS_delay;
  } else {
    if (g_num_threads <= 2) {
      g_delay = 0;
    } else {
      g_delay = g_num_threads * (-FLAGS_delay);
    }
  }

  TSDequeBuffer<uint64_t> *buffer;
  if (FLAGS_list) {
    buffer = new TLLinkedListDequeBuffer<uint64_t>(g_num_threads + 1);
  } else if (FLAGS_2ts) {
    buffer 
      = new TL2TSDequeBuffer<uint64_t>(g_num_threads + 1, g_delay);
  } else {
    buffer = new TLLinkedListDequeBuffer<uint64_t>(g_num_threads + 1);
  }
  TSDeque<uint64_t> *ts =
      new TSDeque<uint64_t>(buffer, timestamping, FLAGS_init_threshold);
  return static_cast<void*>(ts);
}

char* ds_get_stats(void) {
  char buffer[255] = { 0 };
  uint32_t n = snprintf(buffer,
                        sizeof(buffer),
                        "%" PRIu64 "",
                        g_delay);
  if (n != strlen(buffer)) {
    fprintf(stderr, "%s: error creating stats string\n", __func__);
    abort();
  }
  char *newbuf = static_cast<char*>(calloc(
      strlen(buffer) + 1, sizeof(*newbuf)));
  return strncpy(newbuf, buffer, strlen(buffer));
}
