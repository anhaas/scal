#ifndef SCAL_DATASTRUCTURES_TS_DATASTRUCTURE_H_
#define SCAL_DATASTRUCTURES_TS_DATASTRUCTURE_H_

#define __STDC_FORMAT_MACROS 1  // we want PRIu64 and friends
#define __STDC_LIMIT_MACROS
#include <stdint.h>
#include <assert.h>
#include <atomic>
#include <stdio.h>

#include "datastructures/deque.h"
#include "util/time.h"
#include "util/malloc.h"
#include "util/platform.h"

template<typename T, typename TSBuffer, typename Timestamp>
class TSDatastructure : public Deque<T> {
  private:
 
    TSBuffer *buffer_;
    Timestamp *timestamping_;

  public:
    TSDatastructure (uint64_t num_threads, uint64_t delay) {

      timestamping_ = static_cast<Timestamp*>(
          scal::get<Timestamp>(scal::kCachePrefetch * 4));

      timestamping_->initialize(delay, num_threads);

      buffer_ = new TSBuffer(num_threads, timestamping_);
    }

    char* ds_get_stats(void) {
      return buffer_->ds_get_stats();
    }

    // Inserts the element at the left side of the deque.
    bool insert_left(T element) {
      std::atomic<uint64_t> *item = buffer_->insert_left(element);
      timestamping_->set_timestamp(item);
      return true;
    }

    // Inserts the element at the right side of the deque.
    bool insert_right(T element) {
      std::atomic<uint64_t> *item = buffer_->insert_right(element);
      timestamping_->set_timestamp(item);
      return true;
    }

    // Removes the leftmost element from the deque and returns true
    // or returns false if the deque is empty.
    bool remove_left(T *element) {
      // Read the invocation time of this operation, needed for the
      // elimination optimization.
      uint64_t invocation_time[2];
      timestamping_->read_time(invocation_time);
      while (buffer_->try_remove_left(element, invocation_time)) {

        if (*element != (T)NULL) {
          return true;
        }
      }
      return false;
    }

    // Removes the leftmost element from the deque and returns true
    // or returns false if the deque is empty.
    bool remove_right(T *element) {
      // Read the invocation time of this operation, needed for the
      // elimination optimization.
      uint64_t invocation_time[2];
      timestamping_->read_time(invocation_time);
      while (buffer_->try_remove_right(element, invocation_time)) {

        if (*element != (T)NULL) {
          return true;
        }
      }
      return false;
    }
};

#endif  // SCAL_DATASTRUCTURES_TS_DATASTRUCTURE_H_

