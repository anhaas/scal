#ifndef SCAL_DATASTRUCTURES_TS_STACK_H_
#define SCAL_DATASTRUCTURES_TS_STACK_H_

#define __STDC_FORMAT_MACROS 1  // we want PRIu64 and friends
#define __STDC_LIMIT_MACROS
#include <stdint.h>
#include <assert.h>
#include <atomic>
#include <stdio.h>

#include "datastructures/stack.h"
#include "datastructures/ts_timestamp.h"
#include "util/threadlocals.h"
#include "util/time.h"
#include "util/malloc.h"
#include "util/platform.h"
#include "util/random.h"
#include "util/workloads.h"

#define BUFFERSIZE 1000000
#define ABAOFFSET (1ul << 32)

//////////////////////////////////////////////////////////////////////
// The base TSStackBuffer class.
//////////////////////////////////////////////////////////////////////
template<typename T>
class TSStackBuffer {
  public:
    virtual void insert_element(T element, TimeStamp *timestamp) = 0;
    virtual bool try_remove_youngest(T *element, uint64_t *threshold) = 0;
};

//////////////////////////////////////////////////////////////////////
// A TSStackBuffer based on thread-local arrays.
//////////////////////////////////////////////////////////////////////
template<typename T>
class TLArrayStackBuffer : public TSStackBuffer<T> {
  private:
    typedef struct Item {
      std::atomic<T> data;
      std::atomic<uint64_t> timestamp;
      std::atomic<uint64_t> taken;
    } Item;

    // The number of threads.
    uint64_t num_threads_;
    // The thread-local queues, implemented as arrays of size BUFFERSIZE. 
    // At the moment buffer overflows are not considered.
    std::atomic<Item*> **buckets_;
    // The insert pointers of the thread-local queues.
    std::atomic<uint64_t>* *insert_;
    // The pointers for the emptiness check.
    uint64_t* *emptiness_check_pointers_;

    // Returns the oldest not-taken item from the thread-local queue 
    // indicated by thread_id.
    uint64_t get_youngest_item(uint64_t thread_id, Item* *result) {

      uint64_t remove_index = insert_[thread_id]->load();
      uint64_t remove = remove_index - 1;

      *result = NULL;
      if (remove % ABAOFFSET < 1) {
        return remove_index;
      }

      while (remove % ABAOFFSET >= 1) {
        *result = buckets_[thread_id][remove % ABAOFFSET].load(
            std::memory_order_acquire);
        if ((*result)->taken.load(std::memory_order_acquire) == 0) {
          break;
        }
        remove--;
      }

      if (remove % ABAOFFSET < 1) {
        *result = NULL;
        return remove_index;
      }
      return remove;
    }

  public:
    TLArrayStackBuffer(uint64_t num_threads) : num_threads_(num_threads) {
      buckets_ = static_cast<std::atomic<Item*>**>(
          scal::calloc_aligned(num_threads_, sizeof(std::atomic<Item*>*), 
            scal::kCachePrefetch * 2));

      insert_ = static_cast<std::atomic<uint64_t>**>(
          scal::calloc_aligned(num_threads_, 
            sizeof(std::atomic<uint64_t>*), scal::kCachePrefetch * 2));


      emptiness_check_pointers_ = static_cast<uint64_t**>(
          scal::calloc_aligned(num_threads_, sizeof(uint64_t*), 
            scal::kCachePrefetch * 2));

      for (int i = 0; i < num_threads_; i++) {
        buckets_[i] = static_cast<std::atomic<Item*>*>(
            calloc(BUFFERSIZE, sizeof(std::atomic<Item*>)));

        // Add a sentinal node.
        Item *new_item = scal::get<Item>(scal::kCachePrefetch);
        new_item->timestamp.store(0, std::memory_order_release);
        new_item->data.store(0, std::memory_order_release);
        new_item->taken.store(1, std::memory_order_release);
        buckets_[i][0].store(new_item, std::memory_order_release);

        insert_[i] = scal::get<std::atomic<uint64_t>>(
            scal::kCachePrefetch);
        insert_[i]->store(1);

        emptiness_check_pointers_[i] = static_cast<uint64_t*> (
            scal::calloc_aligned(num_threads_, sizeof(uint64_t), 
              scal::kCachePrefetch *2));
      }
    }

    void insert_element(T element, TimeStamp *timestamp) {
      uint64_t thread_id = scal::ThreadContext::get().thread_id();

      Item *new_item = scal::tlget_aligned<Item>(scal::kCachePrefetch);
      new_item->timestamp.store(timestamp->get_timestamp(), 
          std::memory_order_release);
      new_item->data.store(element, std::memory_order_release);
      new_item->taken.store(0, std::memory_order_release);

      // 4) Insert the new item into the thread-local queue.
      uint64_t insert = insert_[thread_id]->load(
          std::memory_order_acquire);

      // Fix the insert pointer.
      for (; insert % ABAOFFSET > 1; insert--) {
        if (buckets_[thread_id][(insert %ABAOFFSET) - 1].load(
              std::memory_order_acquire)
            ->taken.load(std::memory_order_acquire) == 0) {
          break;
        }
      }

      buckets_[thread_id][insert % ABAOFFSET].store(
          new_item, std::memory_order_release);
      insert_[thread_id]->store(
          insert + 1 + ABAOFFSET, std::memory_order_release);
    };

    /////////////////////////////////////////////////////////////////
    //
    /////////////////////////////////////////////////////////////////
    bool try_remove_youngest(T *element, uint64_t *threshold) {
      // Initialize the data needed for the emptiness check.
      uint64_t thread_id = scal::ThreadContext::get().thread_id();
      uint64_t *emptiness_check_pointers = 
        emptiness_check_pointers_[thread_id];
      bool empty = true;
      // The remove_index stores the index where the element is stored in
      // a thread-local buffer.
      uint64_t buffer_array_index = -1;
      // Initialize the result pointer to NULL, which means that no 
      // element has been removed.
      Item *result = NULL;
      // Indicates the index which contains the youngest item.
      uint64_t buffer_index = -1;
      // Stores the time stamp of the youngest item found until now.
      uint64_t timestamp = 0;
      // Stores the value of the remove pointer of a thead-local buffer 
      // before the buffer is actually accessed.
      uint64_t old_remove_index = 0;

      uint64_t start = hwrand();
      // We iterate over all thead-local buffers
      for (uint64_t i = 0; i < num_threads_; i++) {

        Item* item;
        uint64_t tmp_buffer_index = (start + i) % num_threads_;
        // We get the remove/insert pointer of the current thread-local buffer.
        uint64_t tmp_remove_index = 
          insert_[tmp_buffer_index]->load();
        // We get the youngest element from that thread-local buffer.
        uint64_t tmp_buffer_array_index = 
          get_youngest_item(tmp_buffer_index, &item);
        // If we found an element, we compare it to the youngest element 
        // we have found until now.
        if (item != NULL) {
          empty = false;
          uint64_t item_timestamp = 
            item->timestamp.load(std::memory_order_acquire);
          if (timestamp < item_timestamp) {
            // We found a new youngest element, so we remember it.
            result = item;
            buffer_index = tmp_buffer_index;
            timestamp = item_timestamp;
            buffer_array_index = tmp_buffer_array_index;
            old_remove_index = tmp_remove_index;
          } 
          // Check if we can remove the element immediately.
          if (*threshold <= timestamp) {
            uint64_t expected = 0;
            if (result->taken.compare_exchange_weak(
                    expected, 1, 
                    std::memory_order_acq_rel, std::memory_order_relaxed)) {
              // Try to adjust the remove pointer. It does not matter if this 
              // CAS fails.
              insert_[buffer_index]->compare_exchange_weak(
                  old_remove_index, buffer_array_index,
                  std::memory_order_acq_rel, std::memory_order_relaxed);
              *element = result->data.load(std::memory_order_acquire);
              return true;
            }
          }
        } else {
          // No element was found, work on the emptiness check.
          if (emptiness_check_pointers[tmp_buffer_index] 
              != tmp_buffer_array_index) {
            empty = false;
            emptiness_check_pointers[tmp_buffer_index] = 
              tmp_buffer_array_index;
          }
        }
      }
      if (result != NULL) {
        // We found a youngest element which is not younger than the threshold.
        // We try to remove it.
//        uint64_t expected = 0;
//        if (result->taken.compare_exchange_weak(
//                expected, 1, 
//                std::memory_order_acq_rel, std::memory_order_relaxed)) {
//          // Try to adjust the remove pointer. It does not matter if this 
//          // CAS fails.
//          insert_[buffer_index]->compare_exchange_weak(
//              old_remove_index, buffer_array_index,
//              std::memory_order_acq_rel, std::memory_order_relaxed);
//          *element = result->data.load(std::memory_order_acquire);
//          return true;
//        } else {
          // We were not able to remove the youngest element. However, in
          // later invocations we can remove any element younger than the
          // one we found in this iteration. Therefore the time stamp of
          // the element we found in this round will be the threshold for
          // the next round.
          *threshold = result->timestamp.load();
          *element = (T)NULL;
//        }
      }

      *element = 0;
      return !empty;
      return false;
    }
};

//////////////////////////////////////////////////////////////////////
// A TSStackBuffer based on thread-local linked lists.
//////////////////////////////////////////////////////////////////////
template<typename T>
class TLLinkedListStackBuffer : public TSStackBuffer<T> {
  private:
    typedef struct Item {
      std::atomic<Item*> next;
      std::atomic<uint64_t> taken;
      std::atomic<T> data;
      std::atomic<uint64_t> timestamp;
    } Item;

    // The number of threads.
    uint64_t num_threads_;
    // The thread-local queues, implemented as arrays of size BUFFERSIZE. 
    // At the moment buffer overflows are not considered.
    std::atomic<Item*> **buckets_;
    // The pointers for the emptiness check.
    Item** *emptiness_check_pointers_;

    void *get_aba_free_pointer(void *pointer) {
      uint64_t result = (uint64_t)pointer;
      result &= 0xfffffffffffffff8;
      return (void*)result;
    }

    void *add_next_aba(void *pointer, void *old, uint64_t increment) {
      uint64_t aba = (uint64_t)old;
      aba += increment;
      aba &= 0x7;
      uint64_t result = (uint64_t)pointer;
      result = (result & 0xfffffffffffffff8) | aba;
      return (void*)((result & 0xffffffffffffff8) | aba);
    }

    // Returns the oldest not-taken item from the thread-local queue 
    // indicated by thread_id.
    Item* get_youngest_item(uint64_t thread_id) {

      Item* result = (Item*)get_aba_free_pointer(
        buckets_[thread_id]->load());

      while (true) {
        if (result->taken.load() == 0) {
          return result;
        }
        Item *next = result->next.load();
        if (next == result) {
          return NULL;
        }
        result = next;
      }
    }

  public:
    /////////////////////////////////////////////////////////////////
    // Constructor
    /////////////////////////////////////////////////////////////////
    TLLinkedListStackBuffer(uint64_t num_threads) : num_threads_(num_threads) {
      buckets_ = static_cast<std::atomic<Item*>**>(
          scal::calloc_aligned(num_threads_, sizeof(std::atomic<Item*>*), 
            scal::kCachePrefetch * 4));

      emptiness_check_pointers_ = static_cast<Item***>(
          scal::calloc_aligned(num_threads_, sizeof(Item**), 
            scal::kCachePrefetch * 4));

      for (int i = 0; i < num_threads_; i++) {

        buckets_[i] = static_cast<std::atomic<Item*>*>(
            scal::get<std::atomic<Item*>>(scal::kCachePrefetch * 4));

        // Add a sentinal node.
        Item *new_item = scal::get<Item>(scal::kCachePrefetch * 4);
        new_item->timestamp.store(0, std::memory_order_release);
        new_item->data.store(0, std::memory_order_release);
        new_item->taken.store(1, std::memory_order_release);
        new_item->next.store(new_item);
        buckets_[i]->store(new_item, std::memory_order_release);

        emptiness_check_pointers_[i] = static_cast<Item**> (
            scal::calloc_aligned(num_threads_, sizeof(Item*), 
              scal::kCachePrefetch * 4));
      }
    }

    /////////////////////////////////////////////////////////////////
    // insert_element
    /////////////////////////////////////////////////////////////////
    void insert_element(T element, TimeStamp *timestamp) {
      uint64_t thread_id = scal::ThreadContext::get().thread_id();

      Item *new_item = scal::tlget_aligned<Item>(scal::kCachePrefetch);
      new_item->timestamp.store(timestamp->get_timestamp());
      new_item->data.store(element);
      new_item->taken.store(0, std::memory_order_release);

      Item* old_top = buckets_[thread_id]->load();

      Item* top = (Item*)get_aba_free_pointer(old_top);
      while (top->next.load() != top 
          && top->taken.load()) {
        top = top->next.load();
      }

      new_item->next.store(top);
      buckets_[thread_id]->store(
          (Item*) add_next_aba(new_item, old_top, 1));
    };

    /////////////////////////////////////////////////////////////////
    // try_remove_youngest
    /////////////////////////////////////////////////////////////////
    bool try_remove_youngest(T *element, uint64_t *threshold) {
      // Initialize the data needed for the emptiness check.
      uint64_t thread_id = scal::ThreadContext::get().thread_id();
      Item* *emptiness_check_pointers = 
        emptiness_check_pointers_[thread_id];
      bool empty = true;
      // Initialize the result pointer to NULL, which means that no 
      // element has been removed.
      Item *result = NULL;
      // Indicates the index which contains the youngest item.
      uint64_t buffer_index = -1;
      // Stores the time stamp of the youngest item found until now.
      uint64_t timestamp = 0;
      // Stores the value of the remove pointer of a thead-local buffer 
      // before the buffer is actually accessed.
      Item* old_top = NULL;

      uint64_t start = hwrand();
      // We iterate over all thead-local buffers
      for (uint64_t i = 0; i < num_threads_; i++) {

        uint64_t tmp_buffer_index = (start + i) % num_threads_;
        // We get the remove/insert pointer of the current thread-local buffer.
        Item* tmp_top = buckets_[tmp_buffer_index]->load();
        // We get the youngest element from that thread-local buffer.
        Item* item = get_youngest_item(tmp_buffer_index);
        // If we found an element, we compare it to the youngest element 
        // we have found until now.
        if (item != NULL) {
          empty = false;
          uint64_t item_timestamp = 
            item->timestamp.load();
          if (timestamp < item_timestamp) {
            // We found a new youngest element, so we remember it.
            result = item;
            buffer_index = tmp_buffer_index;
            timestamp = item_timestamp;
            old_top = tmp_top;
          } 
          // Check if we can remove the element immediately.
          if (*threshold <= timestamp) {
            uint64_t expected = 0;
            if (result->taken.compare_exchange_weak(expected, 1)) {
              // Try to adjust the remove pointer. It does not matter if 
              // this CAS fails.
              buckets_[buffer_index]->compare_exchange_weak(
                  old_top, (Item*)add_next_aba(result, old_top, 0));

              *element = result->data.load();
              return true;
            }
          }
        } else {
          // No element was found, work on the emptiness check.
          if (emptiness_check_pointers[tmp_buffer_index] 
              != tmp_top) {
            empty = false;
            emptiness_check_pointers[tmp_buffer_index] = 
              tmp_top;
          }
        }
      }
      if (result != NULL) {
        // We found a youngest element which is not younger than the 
        // threshold. We try to remove it.
        uint64_t expected = 0;
        if (result->taken.load() == 0) {
          if (result->taken.compare_exchange_weak(expected, 1)) {
            // Try to adjust the remove pointer. It does not matter if this 
            // CAS fails.
            buckets_[buffer_index]->compare_exchange_weak(
                old_top, (Item*)add_next_aba(result, old_top, 0));
            *element = result->data.load();
            return true;
          }
        }

        *threshold = result->timestamp.load();
        *element = (T)NULL;
      }

      *element = (T)NULL;
      return !empty;
    }
};

//////////////////////////////////////////////////////////////////////
// A TSStackBuffer based on thread-local linked lists.
//////////////////////////////////////////////////////////////////////
template<typename T>
class TL2TSStackBuffer : public TSStackBuffer<T> {
  private:
    typedef struct Item {
      std::atomic<Item*> next;
      std::atomic<uint64_t> taken;
      std::atomic<T> data;
      std::atomic<uint64_t> t1;
      std::atomic<uint64_t> t2;
    } Item;

    // The number of threads.
    uint64_t num_threads_;
    // The thread-local queues, implemented as arrays of size BUFFERSIZE. 
    // At the moment buffer overflows are not considered.
    std::atomic<Item*> **buckets_;
    // The pointers for the emptiness check.
    Item** *emptiness_check_pointers_;
    uint64_t delay_;

    void *get_aba_free_pointer(void *pointer) {
      uint64_t result = (uint64_t)pointer;
      result &= 0xfffffffffffffff8;
      return (void*)result;
    }

    void *add_next_aba(void *pointer, void *old, uint64_t increment) {
      uint64_t aba = (uint64_t)old;
      aba += increment;
      aba &= 0x7;
      uint64_t result = (uint64_t)pointer;
      result = (result & 0xfffffffffffffff8) | aba;
      return (void*)((result & 0xffffffffffffff8) | aba);
    }

    // Returns the oldest not-taken item from the thread-local queue 
    // indicated by thread_id.
    Item* get_youngest_item(uint64_t thread_id) {

      Item* result = (Item*)get_aba_free_pointer(
        buckets_[thread_id]->load(std::memory_order_acquire));

      while (true) {
        if (result->taken.load() == 0) {
          return result;
        }
        Item* next = result->next.load();
        if (next == result) {
          return NULL;
        }
        result = next;
      }
    }

  public:
    /////////////////////////////////////////////////////////////////
    // Constructor
    /////////////////////////////////////////////////////////////////
    TL2TSStackBuffer(uint64_t num_threads, uint64_t delay) : 
      num_threads_(num_threads), delay_(delay) {
      buckets_ = static_cast<std::atomic<Item*>**>(
          scal::calloc_aligned(num_threads_, sizeof(std::atomic<Item*>*), 
            scal::kCachePrefetch * 4));

      emptiness_check_pointers_ = static_cast<Item***>(
          scal::calloc_aligned(num_threads_, sizeof(Item**), 
            scal::kCachePrefetch * 4));

      for (int i = 0; i < num_threads_; i++) {

        buckets_[i] = static_cast<std::atomic<Item*>*>(
            scal::get<std::atomic<Item*>>(scal::kCachePrefetch * 4));

        // Add a sentinal node.
        Item *new_item = scal::get<Item>(scal::kCachePrefetch * 4);
        new_item->t1.store(0, std::memory_order_release);
        new_item->t2.store(0, std::memory_order_release);
        new_item->data.store(0, std::memory_order_release);
        new_item->taken.store(1, std::memory_order_release);
        new_item->next.store(new_item);
        buckets_[i]->store(new_item, std::memory_order_release);

        emptiness_check_pointers_[i] = static_cast<Item**> (
            scal::calloc_aligned(num_threads_, sizeof(Item*), 
              scal::kCachePrefetch * 4));
      }
    }

    /////////////////////////////////////////////////////////////////
    // insert_element
    /////////////////////////////////////////////////////////////////
    void insert_element(T element, TimeStamp *timestamp) {
      uint64_t thread_id = scal::ThreadContext::get().thread_id();

      Item *new_item = scal::tlget_aligned<Item>(scal::kCachePrefetch);
//      new_item->timestamp.store(timestamp, std::memory_order_release);
      new_item->t1.store(UINT64_MAX, std::memory_order_release);
      new_item->t2.store(UINT64_MAX, std::memory_order_release);
      new_item->data.store(element, std::memory_order_release);
      new_item->taken.store(0, std::memory_order_release);

      Item* old_top = buckets_[thread_id]->load(std::memory_order_acquire);

      Item* top = (Item*)get_aba_free_pointer(old_top);
      while (top->next.load() != top 
          && top->taken.load(std::memory_order_acquire)) {
        top = top->next.load();
      }

      new_item->next.store(top);
      buckets_[thread_id]->store(
          (Item*) add_next_aba(new_item, old_top, 1), 
          std::memory_order_release);

      new_item->t1.store(timestamp->get_timestamp());
      uint64_t wait = get_hwtime() + delay_;
      while (get_hwtime() < wait) {}
      new_item->t2.store(timestamp->get_timestamp());
    };

    /////////////////////////////////////////////////////////////////
    // try_remove_youngest
    /////////////////////////////////////////////////////////////////
    bool try_remove_youngest(T *element, uint64_t *threshold) {
      // Initialize the data needed for the emptiness check.
      uint64_t thread_id = scal::ThreadContext::get().thread_id();
      Item* *emptiness_check_pointers = 
        emptiness_check_pointers_[thread_id];
      bool empty = true;
      // Initialize the result pointer to NULL, which means that no 
      // element has been removed.
      Item *result = NULL;
      // Indicates the index which contains the youngest item.
      uint64_t buffer_index = -1;
      // Stores the time stamp of the youngest item found until now.
      uint64_t t1 = 0;
      uint64_t t2 = 0;
      // Stores the value of the remove pointer of a thead-local buffer 
      // before the buffer is actually accessed.
      Item* old_top = NULL;

      uint64_t start = hwrand();
      // We iterate over all thead-local buffers
      for (uint64_t i = 0; i < num_threads_; i++) {

        uint64_t tmp_buffer_index = (start + i) % num_threads_;
        // We get the remove/insert pointer of the current thread-local buffer.
        Item* tmp_top = buckets_[tmp_buffer_index]->load();
        // We get the youngest element from that thread-local buffer.
        Item* item = get_youngest_item(tmp_buffer_index);
        // If we found an element, we compare it to the youngest element 
        // we have found until now.
        if (item != NULL) {
          empty = false;
          uint64_t item_t1 = 
            item->t1.load();
          uint64_t item_t2 = 
            item->t2.load();
          if (t2 < item_t1) {
            // We found a new youngest element, so we remember it.
            result = item;
            buffer_index = tmp_buffer_index;
            t1 = item_t1;
            t2 = item_t2;
            old_top = tmp_top;
           
            // Check if we can remove the element immediately.
            if (*threshold <= t2) {
              uint64_t expected = 0;
              if (result->taken.compare_exchange_weak(
                      expected, 1, 
                      std::memory_order_acq_rel, std::memory_order_relaxed)) {
                // Try to adjust the remove pointer. It does not matter if 
                // this CAS fails.
                buckets_[buffer_index]->compare_exchange_weak(
                    old_top, (Item*)add_next_aba(result, old_top, 0), 
                    std::memory_order_acq_rel, std::memory_order_relaxed);

                *element = result->data.load(std::memory_order_acquire);
                return true;
              }
            }
          }
        } else {
          // No element was found, work on the emptiness check.
          if (emptiness_check_pointers[tmp_buffer_index] 
              != tmp_top) {
            empty = false;
            emptiness_check_pointers[tmp_buffer_index] = 
              tmp_top;
          }
        }
      }
      if (result != NULL) {
        // We found a youngest element which is not younger than the 
        // threshold. We try to remove it.
        uint64_t expected = 0;
        if (result->taken.load() == 0) {
        if (result->taken.compare_exchange_weak(expected, 1)) {
          // Try to adjust the remove pointer. It does not matter if this 
          // CAS fails.
          buckets_[buffer_index]->compare_exchange_weak(
              old_top, (Item*)add_next_aba(result, old_top, 0));
          *element = result->data.load();
          return true;
        }
        }

        uint64_t timestamp = result->t1.load();
        if (timestamp < UINT64_MAX) {
          *threshold = timestamp;
        }
        *element = (T)NULL;
      }

      *element = (T)NULL;
      return !empty;
    }
};

template<typename T>
class TSStack : public Stack<T> {
 private:
  TSStackBuffer<T> *buffer_;
  TimeStamp *timestamping_;
  bool init_threshold_;

  uint64_t* *counter1_;
  uint64_t* *counter2_;
  uint64_t num_threads_;

 public:
  explicit TSStack
    (TSStackBuffer<T> *buffer, TimeStamp *timestamping, 
     bool init_threshold, uint64_t num_threads) 
    : buffer_(buffer), timestamping_(timestamping),
      init_threshold_(init_threshold), num_threads_(num_threads) {

    counter1_ = static_cast<uint64_t**>(
        scal::calloc_aligned(num_threads, sizeof(uint64_t*),
          scal::kCachePrefetch * 4));
    counter2_ = static_cast<uint64_t**>(
        scal::calloc_aligned(num_threads, sizeof(uint64_t*),
          scal::kCachePrefetch * 4));

    for (uint64_t i = 0; i < num_threads; i++) {
      counter1_[i] = scal::get<uint64_t>(scal::kCachePrefetch * 4);
      *(counter1_[i]) = 0;
      counter2_[i] = scal::get<uint64_t>(scal::kCachePrefetch * 4);
      *(counter2_[i]) = 0;
    }
  }
  bool push(T element);
  bool pop(T *element);

  inline void inc_counter1(uint64_t value) {
    uint64_t thread_id = scal::ThreadContext::get().thread_id();
    (*counter1_[thread_id]) += value;
  }
  
  inline void inc_counter2(uint64_t value) {
    uint64_t thread_id = scal::ThreadContext::get().thread_id();
    (*counter2_[thread_id]) += value;
  }
  
  char* ds_get_stats(void) {

    uint64_t sum1 = 0;
    uint64_t sum2 = 0;

    for (int i = 0; i < num_threads_; i++) {
      sum1 += *counter1_[i];
      sum2 += *counter2_[i];
    }

    double avg = sum1;
    avg /= (double)sum2;

    char buffer[255] = { 0 };
    uint32_t n = snprintf(buffer,
                          sizeof(buffer),
                          "tries: %.2f",
                          avg);
    if (n != strlen(buffer)) {
      fprintf(stderr, "%s: error creating stats string\n", __func__);
      abort();
    }
    char *newbuf = static_cast<char*>(calloc(
        strlen(buffer) + 1, sizeof(*newbuf)));
    return strncpy(newbuf, buffer, strlen(buffer));
  }
};

template<typename T>
bool TSStack<T>::push(T element) {
  buffer_->insert_element(element, timestamping_);
  return true;
}

template<typename T>
bool TSStack<T>::pop(T *element) {
  uint64_t threshold;
  uint64_t counter = 0;
  if (init_threshold_) {
    threshold = timestamping_->read_time();
  } else {
    threshold = UINT64_MAX;
  }
  while (buffer_->try_remove_youngest(element, &threshold)) {

    counter++;
    if (*element != (T)NULL) {
      inc_counter1(counter);
      inc_counter2(1);
      return true;
    }
  }
  return false;
}
#endif  // SCAL_DATASTRUCTURES_TS_Stack_H_
