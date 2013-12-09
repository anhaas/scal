#ifndef SCAL_DATASTRUCTURES_TS_TIMESTAMP_H_
#define SCAL_DATASTRUCTURES_TS_TIMESTAMP_H_

#define __STDC_LIMIT_MACROS
#include <stdint.h>
#include <assert.h>
#include <atomic>
#include <stdio.h>

#include "util/threadlocals.h"
#include "util/time.h"
#include "util/malloc.h"
#include "util/platform.h"

//////////////////////////////////////////////////////////////////////
// The base TimeStamp class.
//////////////////////////////////////////////////////////////////////
class TimeStamp {
 public:
  virtual uint64_t get_timestamp() = 0;
  virtual uint64_t read_time() = 0;
};

//////////////////////////////////////////////////////////////////////
// A TimeStamp class based a stuttering counter which requires no
// AWAR or RAW synchronization.
//////////////////////////////////////////////////////////////////////
class StutteringTimeStamp : public TimeStamp {
  private: 
    // The thread-local clocks.
    std::atomic<uint64_t>* *clocks_;
    // The number of threads.
    uint64_t num_threads_;

    inline uint64_t max(uint64_t x, uint64_t y) {
      if (x > y) {
        return x;
      }
      return y;
    }

  public:
    //////////////////////////////////////////////////////////////////////
    // Constructor
    //////////////////////////////////////////////////////////////////////
    StutteringTimeStamp(uint64_t num_threads) 
      : num_threads_(num_threads) {

      clocks_ = static_cast<std::atomic<uint64_t>**>(
          scal::calloc_aligned(num_threads_, 
            sizeof(std::atomic<uint64_t>*), scal::kCachePrefetch));

      for (int i = 0; i < num_threads_; i++) {
        clocks_[i] = 
          scal::get<std::atomic<uint64_t>>(scal::kCachePrefetch);
        clocks_[i]->store(1);
      }
    }
  
    //////////////////////////////////////////////////////////////////////
    // Returns a new time stamp.
    //////////////////////////////////////////////////////////////////////
    uint64_t get_timestamp() {

      uint64_t thread_id = scal::ThreadContext::get().thread_id();
      uint64_t latest_time = 0;

      // Find the latest of all thread-local times.
      for (int i = 0; i < num_threads_; i++) {
        latest_time = 
          max(latest_time, clocks_[i]->load(std::memory_order_acquire));
      }

      // 2) Set the thread-local time to the latest found time + 1.
      clocks_[thread_id]->store(
          latest_time + 1, std::memory_order_release);
      // Return the current local time.
      return latest_time + 1;
    }

    //////////////////////////////////////////////////////////////////////
    // Read the current time.
    //////////////////////////////////////////////////////////////////////
    uint64_t read_time() {

      uint64_t thread_id = scal::ThreadContext::get().thread_id();
      uint64_t latest_time = 0;

      // Find the latest of all thread-local times.
      for (int i = 0; i < num_threads_; i++) {
        latest_time = 
          max(latest_time, clocks_[i]->load(std::memory_order_acquire));
      }

      // Return the current local time.
      return latest_time;
    }
};

//////////////////////////////////////////////////////////////////////
// A TimeStamp class based on an atomic counter
//////////////////////////////////////////////////////////////////////
class AtomicCounterTimeStamp : public TimeStamp {
  private:
    // Memory for the atomic counter.
    std::atomic<uint64_t> *clock_;

  public:
    AtomicCounterTimeStamp() {
      clock_ = scal::get<std::atomic<uint64_t>>(scal::kCachePrefetch * 4);
      clock_->store(1);
    }
  
    uint64_t get_timestamp() {
      return clock_->fetch_add(1);
    }

    uint64_t read_time() {
      return clock_->load();
    }
};

//////////////////////////////////////////////////////////////////////
// A TimeStamp class based on a hardware counter
//////////////////////////////////////////////////////////////////////
class HardwareTimeStamp : public TimeStamp {
  private:


  public:
    HardwareTimeStamp() {
    }
  
    uint64_t get_timestamp() {
      return get_hwtime();
    }
    uint64_t read_time() {
      return get_hwtime();
    }
};

//////////////////////////////////////////////////////////////////////
// A TimeStamp class based on a hardware counter
//////////////////////////////////////////////////////////////////////
class HardwarePTimeStamp : public TimeStamp {
  private:

    uint64_t delay_;

  public:
  
    inline void set_delay(uint64_t delay) {
      delay_ = delay;
    }

    inline void init_sentinel(uint64_t *result) {
      result[0] = 0;
      result[1] = 0;
    }

    inline void init_sentinel_atomic(std::atomic<uint64_t> *result) {
      result[0].store(0);
      result[1].store(0);
    }

    inline void init_top(std::atomic<uint64_t> *result) {
      result[0].store(UINT64_MAX);
      result[1].store(UINT64_MAX);
    }

    inline void load_timestamp(uint64_t *result, std::atomic<uint64_t> *source) {
      result[0] = source[0].load();
      result[1] = source[1].load();
    }

    inline uint64_t get_timestamp() {
      return get_hwptime();
    }

    inline void get_timestamp(std::atomic<uint64_t> *result) {
      result[0].store(get_hwptime());
      uint64_t wait = get_hwtime() + delay_;
      while (get_hwtime() < wait) {}
      result[1].store(get_hwptime());

    }

    inline uint64_t read_time() {
      return get_hwptime();
    }

    inline void read_time(uint64_t *result) {
      result[0] = get_hwptime();
      result[1] = result[0];
    }

    inline bool is_later(uint64_t *timestamp1, uint64_t *timestamp2) {
      return timestamp2[1] < timestamp1[0];
    }
};

//////////////////////////////////////////////////////////////////////
// A TimeStamp class based on a hardware counter
//////////////////////////////////////////////////////////////////////
class ShiftedHardwareTimeStamp : public TimeStamp {
  private:


  public:
    ShiftedHardwareTimeStamp() {
    }
  
    inline uint64_t get_timestamp() {
      return get_hwtime() >> 1;
    }
    inline uint64_t read_time() {
      return get_hwtime() >> 1;
    }
};
#endif // SCAL_DATASTRUCTURES_TS_TIMESTAMP_H_
