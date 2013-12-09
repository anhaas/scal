#ifndef SCAL_DATASTRUCTURES_TS_DEQUE_H_
#define SCAL_DATASTRUCTURES_TS_DEQUE_H_

#define __STDC_LIMIT_MACROS
#include <stdint.h>
#include <assert.h>
#include <atomic>
#include <stdio.h>
#include <time.h>

#include "datastructures/deque.h"
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
// The base TSDequeBuffer class.
//////////////////////////////////////////////////////////////////////
template<typename T>
class TSDequeBuffer {
  public:
    virtual void insert_left(T element, TimeStamp *timestamp) = 0;
    virtual void insert_right(T element, TimeStamp *timestamp) = 0;
    virtual bool try_remove_left
      (T *element, int64_t *threshold) = 0;
    virtual bool try_remove_right
      (T *element, int64_t *threshold) = 0;
};

enum DequeSide {
  kLeft = 0,
  kRight = 1,
  kRandom = 2
};

//////////////////////////////////////////////////////////////////////
// A TSDequeBuffer based on thread-local linked lists.
//////////////////////////////////////////////////////////////////////
template<typename T, typename TimeStamp>
class TL2TSDequeBuffer { //: public TSDequeBuffer<T> {
  private:
    typedef struct Item {
      std::atomic<Item*> left;
      std::atomic<Item*> right;
      std::atomic<uint64_t> taken;
      std::atomic<T> data;
      std::atomic<uint64_t> timestamp[2];
      // Insertion index, needed for the termination condition in 
      // get_left_item. Items inserted at the left get negative
      // indices, items inserted at the right get positive indices.
      std::atomic<int64_t> index;
    } Item;

    // The number of threads.
    uint64_t num_threads_;
    std::atomic<Item*> **left_;
    std::atomic<Item*> **right_;
    int64_t **next_index_;
    // The pointers for the emptiness check.
    Item** *emptiness_check_left_;
    Item** *emptiness_check_right_;
    TimeStamp *timestamping_;

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
    Item* get_left_item(uint64_t thread_id) {

      Item* old_right = right_[thread_id]->load();
      Item* right = (Item*)get_aba_free_pointer(old_right);

      int64_t threshold = right->index.load();

      Item* result = (Item*)get_aba_free_pointer(left_[thread_id]->load());

      // We start at the left pointer and iterate to the right until we
      // find the first item which has not been taken yet.
      while (true) {
        // We reached a node further right than the original right-most 
        // node. We do not have to search any further to the right, we
        // will not take the element anyways.
        if (result->index.load() > threshold) {
          return NULL;
        }
        // We found a good node, return it.
        if (result->taken.load() == 0) {
          return result;
        }
        // We have reached the end of the list and found nothing, so we
        // return NULL.
        if (result->right.load() == result) {
          return NULL;
        }
        result = result->right.load();
      }
    }

    Item* get_right_item(uint64_t thread_id) {

      Item* old_left = left_[thread_id]->load();
      Item* left = (Item*)get_aba_free_pointer(old_left);

      int64_t threshold = left->index.load();

      Item* result = (Item*)get_aba_free_pointer(
        right_[thread_id]->load());

      // We start at the right pointer and iterate to the left until we
      // find the first item which has not been taken yet.
      while (true) {
        // We reached a node further left than the original left-most 
        // node. We do not have to search any further to the left, we
        // will not take the element anyways.
        if (result->index.load() < threshold) {
          return NULL;
        }
        // We found a good node, return it.
        if (result->taken.load() == 0) {
          return result;
        }
        // We have reached the end of the list and found nothing, so we
        // return NULL.
        if (result->left.load() == result) {
          return NULL;
        }
        result = result->left.load();
      }
    }

  public:
    /////////////////////////////////////////////////////////////////
    // Constructor
    /////////////////////////////////////////////////////////////////
    TL2TSDequeBuffer(uint64_t num_threads, TimeStamp *timestamping) 
      : num_threads_(num_threads), timestamping_(timestamping) {
      left_ = static_cast<std::atomic<Item*>**>(
          scal::calloc_aligned(num_threads_, sizeof(std::atomic<Item*>*), 
            scal::kCachePrefetch * 4));

      right_ = static_cast<std::atomic<Item*>**>(
          scal::calloc_aligned(num_threads_, sizeof(std::atomic<Item*>*), 
            scal::kCachePrefetch * 4));

      next_index_ = static_cast<int64_t**>(
          scal::calloc_aligned(num_threads_, sizeof(int64_t*), 
            scal::kCachePrefetch * 4));

      emptiness_check_left_ = static_cast<Item***>(
          scal::calloc_aligned(num_threads_, sizeof(Item**), 
            scal::kCachePrefetch * 4));

      emptiness_check_right_ = static_cast<Item***>(
          scal::calloc_aligned(num_threads_, sizeof(Item**), 
            scal::kCachePrefetch * 4));

      for (int i = 0; i < num_threads_; i++) {

        left_[i] = static_cast<std::atomic<Item*>*>(
            scal::get<std::atomic<Item*>>(scal::kCachePrefetch * 4));

        right_[i] = static_cast<std::atomic<Item*>*>(
            scal::get<std::atomic<Item*>>(scal::kCachePrefetch * 4));

        next_index_[i] = static_cast<int64_t*>(
            scal::get<int64_t>(scal::kCachePrefetch * 4));

        // Add a sentinal node.
        Item *new_item = scal::get<Item>(scal::kCachePrefetch * 4);
        timestamping_->init_sentinel_atomic(new_item->timestamp);
        new_item->data.store(0);
        new_item->taken.store(1);
        new_item->left.store(new_item);
        new_item->right.store(new_item);
        new_item->index.store(0);
        left_[i]->store(new_item);
        right_[i]->store(new_item);
        *next_index_[i] = 1;

        emptiness_check_left_[i] = static_cast<Item**> (
            scal::calloc_aligned(num_threads_, sizeof(Item*), 
              scal::kCachePrefetch * 4));

        emptiness_check_right_[i] = static_cast<Item**> (
            scal::calloc_aligned(num_threads_, sizeof(Item*), 
              scal::kCachePrefetch * 4));
      }
    }

    char* ds_get_stats(void) {
      return NULL;
    }

    /////////////////////////////////////////////////////////////////
    // insert_left
    /////////////////////////////////////////////////////////////////
    void insert_left(T element) {
      uint64_t thread_id = scal::ThreadContext::get().thread_id();

      Item *new_item = scal::tlget_aligned<Item>(scal::kCachePrefetch);
      // Switch the sign of the time stamp of elements inserted at the
      // left side.
      timestamping_->init_top(new_item->timestamp);
      new_item->data.store(element);
      new_item->taken.store(0);
      new_item->left.store(new_item);
      new_item->index = -((*next_index_[thread_id])++);

      Item* old_left = left_[thread_id]->load();

      Item* left = (Item*)get_aba_free_pointer(old_left);
      while (left->right.load() != left 
          && left->taken.load()) {
        left = left->right.load();
      }

      if (left->right.load() == left) {
        // The buffer is empty. We have to increase the aba counter of the
        // right pointer too to guarantee that a pending right-pointer
        // update of a remove operation does not make the left and the
        // right pointer point to different lists.
        Item* old_right = right_[thread_id]->load();
        right_[thread_id]->store((Item*) add_next_aba(left, old_right, 1));
      }

      new_item->right.store(left);
      left->left.store(new_item);
      left_[thread_id]->store(
        (Item*) add_next_aba(new_item, old_left, 1));
 
      timestamping_->get_timestamp(new_item->timestamp);
    }

    /////////////////////////////////////////////////////////////////
    // insert_right
    /////////////////////////////////////////////////////////////////
    void insert_right(T element) {
      uint64_t thread_id = scal::ThreadContext::get().thread_id();

      Item *new_item = scal::tlget_aligned<Item>(scal::kCachePrefetch);
      timestamping_->init_top(new_item->timestamp);
      new_item->data.store(element);
      new_item->taken.store(0);
      new_item->right.store(new_item);
      new_item->index = (*next_index_[thread_id])++;

      Item* old_right = right_[thread_id]->load();

      Item* right = (Item*)get_aba_free_pointer(old_right);
      while (right->left.load() != right 
          && right->taken.load()) {
        right = right->left.load();
      }

      if (right->left.load() == right) {
        // The buffer is empty. We have to increase the aba counter of the
        // left pointer too to guarantee that a pending left-pointer
        // update of a remove operation does not make the left and the
        // right pointer point to different lists.
        Item* old_left = left_[thread_id]->load();
        left_[thread_id]->store( (Item*) add_next_aba(right, old_left, 1)); }

      new_item->left.store(right);
      right->right.store(new_item);
      right_[thread_id]->store(
        (Item*) add_next_aba(new_item, old_right, 1));

      timestamping_->get_timestamp(new_item->timestamp);
    }

    inline bool inserted_left(Item *item) {
      return item->index.load() < 0;
    }

    inline bool inserted_right(Item *item) {
      return item->index.load() > 0;
    }

    inline bool is_more_left(Item *item1, uint64_t *timestamp1, Item *item2, uint64_t *timestamp2) {
      if (inserted_left(item2)) {
        if (inserted_left(item1)) {
          return timestamping_->is_later(timestamp1, timestamp2);
        } else {
          return false;
        }
      } else {
        if (inserted_left(item1)) {
          return true;
        } else {
          return timestamping_->is_later(timestamp2, timestamp1);
        }
      }
    }

    inline bool is_more_right(Item *item1, uint64_t *timestamp1, Item *item2, uint64_t *timestamp2) {
      if (inserted_right(item2)) {
        if (inserted_right(item1)) {
          return timestamping_->is_later(timestamp1, timestamp2);
        } else {
          return false;
        }
      } else {
        if (inserted_right(item1)) {
          return true;
        } else {
          return timestamping_->is_later(timestamp2, timestamp1);
        }
      }
    }

    /////////////////////////////////////////////////////////////////
    // try_remove_left
    /////////////////////////////////////////////////////////////////
    bool try_remove_left
      (T *element, uint64_t *invocation_time) {
      // Initialize the data needed for the emptiness check.
      uint64_t thread_id = scal::ThreadContext::get().thread_id();
      Item* *emptiness_check_left = 
        emptiness_check_left_[thread_id];
      Item* *emptiness_check_right = 
        emptiness_check_right_[thread_id];
      bool empty = true;
      // Initialize the result pointer to NULL, which means that no 
      // element has been removed.
      Item *result = NULL;
      // Indicates the index which contains the youngest item.
      uint64_t buffer_index = -1;
      // Stores the time stamp of the left-most item found until now.
      uint64_t tmp_timestamp[2][2];
      uint64_t tmp_index = 1;
      timestamping_->init_sentinel(tmp_timestamp[0]);
      uint64_t *timestamp = tmp_timestamp[0];
      // Stores the value of the remove pointer of a thead-local buffer 
      // before the buffer is actually accessed.
      Item* old_left = NULL;

      uint64_t start_time[2];
      timestamping_->read_time(start_time);
      uint64_t start = hwrand();
      // We iterate over all thead-local buffers
      for (uint64_t i = 0; i < num_threads_ / 2 +1; i++) {

        uint64_t tmp_buffer_index = (start + i) % (num_threads_/ 2 + 1);
        // We get the remove/insert pointer of the current thread-local buffer.
        Item* tmp_left = left_[tmp_buffer_index]->load();
        // We get the youngest element from that thread-local buffer.
        Item* item = get_left_item(tmp_buffer_index);
        // If we found an element, we compare it to the youngest element 
        // we have found until now.
        if (item != NULL) {
          empty = false;
          uint64_t *item_timestamp;
          timestamping_->load_timestamp(tmp_timestamp[tmp_index], item->timestamp);
          item_timestamp = tmp_timestamp[tmp_index];
          
          if (result == NULL || is_more_left(item, item_timestamp, result, timestamp)) {
            // We found a new youngest element, so we remember it.
            result = item;
            buffer_index = tmp_buffer_index;
            timestamp = item_timestamp;
            tmp_index ^=1;
            old_left = tmp_left;
           
            // Check if we can remove the element immediately.
            if (inserted_left(result) && !timestamping_->is_later(invocation_time, timestamp)) {
              uint64_t expected = 0;
              if (result->taken.load() == 0) {
                if (result->taken.compare_exchange_weak(
                    expected, 1)) {
                  // Try to adjust the remove pointer. It does not matter if 
                  // this CAS fails.
                  left_[buffer_index]->compare_exchange_weak(
                      old_left, (Item*)add_next_aba(result, old_left, 0));

                  *element = result->data.load();
                  return true;
                }
              }
            }
          }
        } else {
          // No element was found, work on the emptiness check.
          if (emptiness_check_left[tmp_buffer_index] 
              != tmp_left) {
            empty = false;
            emptiness_check_left[tmp_buffer_index] = 
              tmp_left;
          }
          Item* tmp_right = right_[tmp_buffer_index]->load();
          if (emptiness_check_right[tmp_buffer_index] 
              != tmp_right) {
            empty = false;
            emptiness_check_right[tmp_buffer_index] = 
              tmp_right;
          }
        }
      }
      if (result != NULL) {
        if (!timestamping_->is_later(timestamp, start_time)) {
          uint64_t expected = 0;
          if (result->taken.load() == 0) {
            if (result->taken.compare_exchange_weak(
                    expected, 1)) {
              // Try to adjust the remove pointer. It does not matter if this 
              // CAS fails.
              left_[buffer_index]->compare_exchange_weak(
                  old_left, (Item*)add_next_aba(result, old_left, 0));
              *element = result->data.load();
              return true;
            }
          }
        }
      }

      *element = (T)NULL;
      return !empty;
    }

    /////////////////////////////////////////////////////////////////
    // try_remove_right
    ////////////////////////////////////////////////////////////////
    bool try_remove_right
      (T *element, uint64_t *invocation_time) {
      // Initialize the data needed for the emptiness check.
      uint64_t thread_id = scal::ThreadContext::get().thread_id();
      Item* *emptiness_check_left = 
        emptiness_check_left_[thread_id];
      Item* *emptiness_check_right = 
        emptiness_check_right_[thread_id];
      bool empty = true;
      // Initialize the result pointer to NULL, which means that no 
      // element has been removed.
      Item *result = NULL;
      // Indicates the index which contains the youngest item.
      uint64_t buffer_index = -1;
      // Stores the time stamp of the right-most item found until now.
      uint64_t tmp_timestamp[2][2];
      uint64_t tmp_index = 1;
      timestamping_->init_sentinel(tmp_timestamp[0]);
      uint64_t *timestamp = tmp_timestamp[0];
      // Stores the value of the remove pointer of a thead-local buffer 
      // before the buffer is actually accessed.
      Item* old_right = NULL;

      uint64_t start_time[2];
      timestamping_->read_time(start_time);
      uint64_t start = hwrand();
      // We iterate over all thead-local buffers
      for (uint64_t i = 0; i < num_threads_; i++) {

        uint64_t tmp_buffer_index = (start + i) % num_threads_;
        // We get the remove/insert pointer of the current thread-local buffer.
        Item* tmp_right = right_[tmp_buffer_index]->load();
        // We get the youngest element from that thread-local buffer.
        Item* item = get_right_item(tmp_buffer_index);
        // If we found an element, we compare it to the youngest element 
        // we have found until now.
        if (item != NULL) {
          empty = false;
          uint64_t *item_timestamp;
          timestamping_->load_timestamp(tmp_timestamp[tmp_index], item->timestamp);
          item_timestamp = tmp_timestamp[tmp_index];
          
          if (result == NULL || is_more_right(item, item_timestamp, result, timestamp)) {
            // We found a new youngest element, so we remember it.
            result = item;
            buffer_index = tmp_buffer_index;
            timestamp = item_timestamp;
            tmp_index ^=1;
            old_right = tmp_right;

            // Check if we can remove the element immediately.
            if (inserted_right(result) && !timestamping_->is_later(invocation_time, timestamp)) {
              uint64_t expected = 0;
              if (result->taken.load() == 0) {
                if (result->taken.compare_exchange_weak(
                      expected, 1)) {

                  // Try to adjust the remove pointer. It does not matter if 
                  // this CAS fails.
                  right_[buffer_index]->compare_exchange_weak(
                      old_right, (Item*)add_next_aba(result, old_right, 0));

                  *element = result->data.load();
                  return true;
                }
              }
            }
          }
        } else {
          // No element was found, work on the emptiness check.
          if (emptiness_check_right[tmp_buffer_index] 
              != tmp_right) {
            empty = false;
            emptiness_check_right[tmp_buffer_index] = 
              tmp_right;
          }
          Item* tmp_left = left_[tmp_buffer_index]->load();
          if (emptiness_check_left[tmp_buffer_index] 
              != tmp_left) {
            empty = false;
            emptiness_check_left[tmp_buffer_index] = 
              tmp_left;
          }
        }
      }
      if (result != NULL) {
        if (!timestamping_->is_later(timestamp, start_time)) {
          uint64_t expected = 0;
          if (result->taken.load() == 0) {
            if (result->taken.compare_exchange_weak(
                    expected, 1)) {
              // Try to adjust the remove pointer. It does not matter if
              // this CAS fails.
              right_[buffer_index]->compare_exchange_weak(
                  old_right, (Item*)add_next_aba(result, old_right, 0));
              *element = result->data.load();
              return true;
            }
          }
        }
      }

      *element = (T)NULL;
      return !empty;
    }
};

//////////////////////////////////////////////////////////////////////
// A TSDequeBuffer based on thread-local linked lists.
//////////////////////////////////////////////////////////////////////
template<typename T>
class TLLinkedListDequeBuffer : public TSDequeBuffer<T> {
  private:
    typedef struct Item {
      std::atomic<Item*> left;
      std::atomic<Item*> right;
      std::atomic<uint64_t> taken;
      std::atomic<T> data;
      std::atomic<int64_t> timestamp;
    } Item;

    // The number of threads.
    uint64_t num_threads_;
    std::atomic<Item*> **left_;
    std::atomic<Item*> **right_;
    // The pointers for the emptiness check.
    Item** *emptiness_check_left_;
    Item** *emptiness_check_right_;

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
    Item* get_left_item(uint64_t thread_id) {

      Item* old_right = right_[thread_id]->load();
      Item* right = (Item*)get_aba_free_pointer(old_right);

      int64_t threshold = right->timestamp.load();

      Item* result = (Item*)get_aba_free_pointer(left_[thread_id]->load());

      // We start at the left pointer and iterate to the right until we
      // find the first item which has not been taken yet.
      while (true) {
        // We reached a node further left than the original left-most 
        // node. We do not have to search any further to the left, we
        // will not take the element anyways.
        if (result->timestamp.load() > threshold) {
          return NULL;
        }
        // We found a good node, return it.
        if (result->taken.load() == 0) {
          return result;
        }
        // We have reached the end of the list and found nothing, so we
        // return NULL.
        if (result->left.load() == result) {
          return NULL;
        }
        result = result->left.load();
      }
    }

    Item* get_right_item(uint64_t thread_id) {

      Item* left = (Item*)get_aba_free_pointer(
          left_[thread_id]->load());

      int64_t threshold = left->timestamp.load();

      Item* result = (Item*)get_aba_free_pointer(
        right_[thread_id]->load());

      // We start at the right pointer and iterate to the left until we find
      // the first item which has not been taken yet.
      while (true) {
        // We reached a node further left than the original left-most 
        // node. We do not have to search any further to the left, we
        // will not take the element anyways.
        if (result->timestamp.load() < threshold) {
          return NULL;
        }
        // We found a good node, return it.
        if (result->taken.load() == 0) {
          return result;
        }
        // We have reached the end of the list and found nothing, so we
        // return NULL.
        if (result->left.load() == result) {
          return NULL;
        }
        result = result->left.load();
      }
    }

  public:
    /////////////////////////////////////////////////////////////////
    // Constructor
    /////////////////////////////////////////////////////////////////
    TLLinkedListDequeBuffer(uint64_t num_threads) : num_threads_(num_threads) {
      left_ = static_cast<std::atomic<Item*>**>(
          scal::calloc_aligned(num_threads_, sizeof(std::atomic<Item*>*), 
            scal::kCachePrefetch * 4));

      right_ = static_cast<std::atomic<Item*>**>(
          scal::calloc_aligned(num_threads_, sizeof(std::atomic<Item*>*), 
            scal::kCachePrefetch * 4));

      emptiness_check_left_ = static_cast<Item***>(
          scal::calloc_aligned(num_threads_, sizeof(Item**), 
            scal::kCachePrefetch * 4));

      emptiness_check_right_ = static_cast<Item***>(
          scal::calloc_aligned(num_threads_, sizeof(Item**), 
            scal::kCachePrefetch * 4));

      for (int i = 0; i < num_threads_; i++) {

        left_[i] = static_cast<std::atomic<Item*>*>(
            scal::get<std::atomic<Item*>>(scal::kCachePrefetch * 4));

        right_[i] = static_cast<std::atomic<Item*>*>(
            scal::get<std::atomic<Item*>>(scal::kCachePrefetch * 4));

        // Add a sentinal node.
        Item *new_item = scal::get<Item>(scal::kCachePrefetch * 4);
        new_item->timestamp.store(0);
        new_item->data.store(0);
        new_item->taken.store(1);
        new_item->left.store(new_item);
        new_item->right.store(new_item);
        left_[i]->store(new_item);
        right_[i]->store(new_item);

        emptiness_check_left_[i] = static_cast<Item**> (
            scal::calloc_aligned(num_threads_, sizeof(Item*), 
              scal::kCachePrefetch * 4));

        emptiness_check_right_[i] = static_cast<Item**> (
            scal::calloc_aligned(num_threads_, sizeof(Item*), 
              scal::kCachePrefetch * 4));
      }
    }

    /////////////////////////////////////////////////////////////////
    // insert_left
    /////////////////////////////////////////////////////////////////
    void insert_left(T element, TimeStamp *timestamp) {
      uint64_t thread_id = scal::ThreadContext::get().thread_id();

      Item *new_item = scal::tlget_aligned<Item>(scal::kCachePrefetch);
      // Switch the sign of the time stamp of elements inserted at the left side.
      new_item->timestamp.store(
          ((int64_t)timestamp->get_timestamp()) * (-1));
      new_item->data.store(element);
      new_item->taken.store(0);
      new_item->left.store(new_item);

      Item* old_left = left_[thread_id]->load();

      Item* left = (Item*)get_aba_free_pointer(old_left);
      while (left->right.load() != left 
          && left->taken.load()) {
        left = left->right.load();
      }

      if (left->right.load() == left) {
        // The buffer is empty. We have to increase the aba counter of the right
        // pointer too to guarantee that a pending right-pointer update of a
        // remove operation does not make the left and the right pointer point
        // to different lists.
        Item* old_right = right_[thread_id]->load();
        right_[thread_id]->store( (Item*) add_next_aba(left, old_right, 1));
      }

      new_item->right.store(left);
      left->left.store(new_item);
      left_[thread_id]->store(
        (Item*) add_next_aba(new_item, old_left, 1));
    }

    /////////////////////////////////////////////////////////////////
    // insert_right
    /////////////////////////////////////////////////////////////////
    void insert_right(T element, TimeStamp *timestamp) {
      uint64_t thread_id = scal::ThreadContext::get().thread_id();

      Item *new_item = scal::tlget_aligned<Item>(scal::kCachePrefetch);
      new_item->timestamp.store(timestamp->get_timestamp());
      new_item->data.store(element);
      new_item->taken.store(0);
      new_item->right.store(new_item);

      Item* old_right = right_[thread_id]->load();

      Item* right = (Item*)get_aba_free_pointer(old_right);
      while (right->left.load() != right 
          && right->taken.load()) {
        right = right->left.load();
      }

      if (right->left.load() == right) {
        // The buffer is empty. We have to increase the aba counter of the left
        // pointer too to guarantee that a pending left-pointer update of a
        // remove operation does not make the left and the right pointer point
        // to different lists.
        Item* old_left = left_[thread_id]->load();
        left_[thread_id]->store( (Item*) add_next_aba(right, old_left, 1)); }

      new_item->left.store(right);
      right->right.store(new_item);
      right_[thread_id]->store(
        (Item*) add_next_aba(new_item, old_right, 1));
    }

    /////////////////////////////////////////////////////////////////
    // try_remove_left
    /////////////////////////////////////////////////////////////////
    bool try_remove_left
      (T *element, int64_t *threshold) {
      // Initialize the data needed for the emptiness check.
      uint64_t thread_id = scal::ThreadContext::get().thread_id();
      Item* *emptiness_check_left = 
        emptiness_check_left_[thread_id];
      Item* *emptiness_check_right = 
        emptiness_check_right_[thread_id];
      bool empty = true;
      // Initialize the result pointer to NULL, which means that no 
      // element has been removed.
      Item *result = NULL;
      // Indicates the index which contains the youngest item.
      uint64_t buffer_index = -1;
      // Stores the time stamp of the left-most item found until now.
      int64_t timestamp = INT64_MAX;
      // Stores the value of the remove pointer of a thead-local buffer 
      // before the buffer is actually accessed.
      Item* old_left = NULL;

      uint64_t start = hwrand();
      // We iterate over all thead-local buffers
      for (uint64_t i = 0; i < num_threads_; i++) {

        uint64_t tmp_buffer_index = (start + i) % num_threads_;
        // We get the remove/insert pointer of the current thread-local buffer.
        Item* tmp_left = left_[tmp_buffer_index]->load();
        // We get the youngest element from that thread-local buffer.
        Item* item = get_left_item(tmp_buffer_index);
        // If we found an element, we compare it to the youngest element 
        // we have found until now.
        if (item != NULL) {
          empty = false;
          int64_t item_timestamp = 
            item->timestamp.load();

          if (timestamp > item_timestamp) {
            // We found a new youngest element, so we remember it.
            result = item;
            buffer_index = tmp_buffer_index;
            timestamp = item_timestamp;
            old_left = tmp_left;
          } 
          // Check if we can remove the element immediately.
          if (threshold[0] > timestamp) {
            uint64_t expected = 0;
            if (result->taken.load() == 0) {
              if (result->taken.compare_exchange_weak(
                  expected, 1)) {
                // Try to adjust the remove pointer. It does not matter if 
                // this CAS fails.
                left_[buffer_index]->compare_exchange_weak(
                    old_left, (Item*)add_next_aba(result, old_left, 0));

                *element = result->data.load();
                return true;
              }
            }
          }
        } else {
          // No element was found, work on the emptiness check.
          if (emptiness_check_left[tmp_buffer_index] 
              != tmp_left) {
            empty = false;
            emptiness_check_left[tmp_buffer_index] = 
              tmp_left;
          }
          Item* tmp_right = right_[tmp_buffer_index]->load();
          if (emptiness_check_right[tmp_buffer_index] 
              != tmp_right) {
            empty = false;
            emptiness_check_right[tmp_buffer_index] = 
              tmp_right;
          }
        }
      }
      if (result != NULL) {
        // (timestamp == INT64_MAX) means that the element was inserted at
        // the right side and did not get a time stamp yet. We should not
        // take it then.
        if ((timestamp != INT64_MAX && timestamp <= threshold[0])
            || (timestamp > 0 && timestamp < threshold[1])
            || (timestamp < 0 && timestamp > -threshold[1])
            ){
          // We found a similar element to the one in the last iteration. Let's
          // try to remove it
          uint64_t expected = 0;
            if (result->taken.load() == 0) {
          if (result->taken.compare_exchange_weak(
                  expected, 1)) {
            // Try to adjust the remove pointer. It does not matter if this 
            // CAS fails.
            left_[buffer_index]->compare_exchange_weak(
                old_left, (Item*)add_next_aba(result, old_left, 0));
            *element = result->data.load();
            return true;
          }
            }
        }
        // We only set an new threshold if the element we tried to remove
        // already had a time stamp. Otherwise we continue to use the old
        // time stamp. 
        if (timestamp != INT64_MAX) {
          threshold[0] =  timestamp;
        }
      }

      *element = (T)NULL;
      return !empty;
    }

    /////////////////////////////////////////////////////////////////
    // try_remove_right
    ////////////////////////////////////////////////////////////////
    bool try_remove_right(T *element, int64_t *threshold) {
      // Initialize the data needed for the emptiness check.
      uint64_t thread_id = scal::ThreadContext::get().thread_id();
      Item* *emptiness_check_left = 
        emptiness_check_left_[thread_id];
      Item* *emptiness_check_right = 
        emptiness_check_right_[thread_id];
      bool empty = true;
      // Initialize the result pointer to NULL, which means that no 
      // element has been removed.
      Item *result = NULL;
      // Indicates the index which contains the youngest item.
      uint64_t buffer_index = -1;
      // Stores the time stamp of the right-most item found until now.
      int64_t timestamp = INT64_MIN;
      // Stores the value of the remove pointer of a thead-local buffer 
      // before the buffer is actually accessed.
      Item* old_right = NULL;

      uint64_t start = hwrand();
      // We iterate over all thead-local buffers
      for (uint64_t i = 0; i < num_threads_; i++) {

        uint64_t tmp_buffer_index = (start + i) % num_threads_;
        // We get the remove/insert pointer of the current thread-local buffer.
        Item* tmp_right = right_[tmp_buffer_index]->load();
        // We get the youngest element from that thread-local buffer.
        Item* item = get_right_item(tmp_buffer_index);
        // If we found an element, we compare it to the youngest element 
        // we have found until now.
        if (item != NULL) {
          empty = false;
          int64_t item_timestamp = 
            item->timestamp.load();
          if (timestamp < item_timestamp) {
            // We found a new youngest element, so we remember it.
            result = item;
            buffer_index = tmp_buffer_index;
            timestamp = item_timestamp;
            old_right = tmp_right;
          }
          // Check if we can remove the element immediately.
          if (threshold[0] < timestamp) {
            uint64_t expected = 0;
            if (result->taken.load() == 0) {
              if (result->taken.compare_exchange_weak(
                    expected, 1)) {

                // Try to adjust the remove pointer. It does not matter if 
                // this CAS fails.
                right_[buffer_index]->compare_exchange_weak(
                    old_right, (Item*)add_next_aba(result, old_right, 0));

                *element = result->data.load();
                return true;
              }
            }
          }
        } else {
          // No element was found, work on the emptiness check.
          if (emptiness_check_right[tmp_buffer_index] 
              != tmp_right) {
            empty = false;
            emptiness_check_right[tmp_buffer_index] = 
              tmp_right;
          }
          Item* tmp_left = left_[tmp_buffer_index]->load();
          if (emptiness_check_left[tmp_buffer_index] 
              != tmp_left) {
            empty = false;
            emptiness_check_left[tmp_buffer_index] = 
              tmp_left;
          }
        }
      }
      if (result != NULL) {
        // (timestamp == INT64_MIN) means that the element was inserted at
        // the left side and did not get a time stamp yet. We should not
        // take it then.
        if ((timestamp != INT64_MIN && timestamp >= threshold[0])
            || (timestamp > 0 && timestamp < threshold[1])
            || (timestamp < 0 && timestamp > -threshold[1])
            ){
          // We found a similar element to the one in the last iteration. Let's
          // try to remove it
          uint64_t expected = 0;
          if (result->taken.load() == 0) {
            if (result->taken.compare_exchange_weak(
                    expected, 1)) {
              // Try to adjust the remove pointer. It does not matter if
              // this CAS fails.
              right_[buffer_index]->compare_exchange_weak(
                  old_right, (Item*)add_next_aba(result, old_right, 0));
              *element = result->data.load();
              return true;
            }
          }
        }
        // We only set an new threshold if the element we tried to remove
        // already had a time stamp. Otherwise we continue to use the old
        // time stamp. 
        if (timestamp != INT64_MIN) {
          threshold[0] = timestamp;
        }
      }

      *element = (T)NULL;
      return !empty;
    }
};

template<typename T>
class TSDeque : public Deque<T> {
 private:
  TSDequeBuffer<T> *buffer_;
  TimeStamp *timestamping_;
  bool init_threshold_;
  uint64_t num_threads_;
  uint64_t* *counter1_;
  uint64_t* *counter2_;

 public:
  explicit TSDeque
    (TSDequeBuffer<T> *buffer, TimeStamp *timestamping, 
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
  bool insert_left(T item);
  bool remove_left(T *item);
  bool insert_right(T item);
  bool remove_right(T *item);

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
bool TSDeque<T>::insert_left(T element) {
  uint64_t timestamp = timestamping_->get_timestamp();
  buffer_->insert_left(element, timestamping_);
  return true;
}

template<typename T>
bool TSDeque<T>::insert_right(T element) {
  buffer_->insert_right(element, timestamping_);
  return true;
}

template<typename T>
bool TSDeque<T>::remove_left(T *element) {
  int64_t threshold[2];
  uint64_t counter = 0;
  if (init_threshold_) {
    //threshold[1] is positive because defines a point in time independent
    //of the side of the insert.
    threshold[1] = timestamping_->read_time();
    threshold[0] = -threshold[1];
  } else {
    threshold[0] = INT64_MIN;
    //threshold[1] is INT64_MIN because we want any insert operation to
    //start after threshold[1].
    threshold[1] = INT64_MIN;
  }

  while (
    buffer_->try_remove_left(element, threshold)) {
    counter++;
    if (*element != (T)NULL) {
      inc_counter1(counter);
      inc_counter2(1);
      return true;
    }
  }
  return false;
}

template<typename T>
bool TSDeque<T>::remove_right(T *element) {
  int64_t threshold[2];
  uint64_t counter = 0;
  if (init_threshold_) {
    //threshold[1] is positive because defines a point in time independent
    //of the side of the insert.
    threshold[1] = timestamping_->read_time();
    threshold[0] = threshold[1];
  } else {
    threshold[0] = INT64_MAX;
    //threshold[1] is INT64_MIN because we want any insert operation to
    //start after threshold[1].
    threshold[1] = INT64_MIN;
  }

  while (
    buffer_->try_remove_right(element, threshold)) {
    counter++;
    if (*element != (T)NULL) {
      inc_counter1(counter);
      inc_counter2(1);
      return true;
    }
  }
  return false;
}
#endif  // SCAL_DATASTRUCTURES_TS_DEQUE_H_
