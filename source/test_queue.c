#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "coroed/core/queue.h"

void test_queue() {
  struct queue queue;
  struct queue_node a, b, c;

  queue_init(&queue);
  assert(empty(&queue));
  assert(queue.size == 0);
  assert(queue.head == NULL && queue.tail == NULL);
  assert(queue_pop_front(&queue) == NULL);

  queue_push_back(&queue, &a);
  queue_push_back(&queue, &b);
  queue_push_back(&queue, &c);
  assert(queue.size == 3);
  assert(queue.head == &a && queue.tail == &c);
  assert(queue_pop_front(&queue) == &a);
  assert(a.next == NULL && queue.size == 2);
  assert(queue_pop_front(&queue) == &b);
  assert(b.next == NULL && queue.size == 1);
  assert(queue_pop_front(&queue) == &c);
  assert(c.next == NULL && queue.size == 0);
  assert(empty(&queue));
  assert(queue.head == NULL && queue.tail == NULL);
  assert(queue_pop_front(&queue) == NULL);

  enum { CAPACITY = 64, OPERATIONS = 20000 };
  struct queue_node nodes[CAPACITY];
  bool enqueued[CAPACITY] = {false};
  size_t expected[CAPACITY];
  size_t head = 0;
  size_t count = 0;
  uint32_t random_state = 213;

  queue_init(&queue);
  for (size_t operation = 0; operation < OPERATIONS; ++operation) {
    random_state = random_state * 1103515245u + 12345u;
    bool push = count == 0 ||
                (count < CAPACITY && ((random_state >> 16) & 3u) != 0);

    if (push) {
      size_t index = (random_state >> 8) % CAPACITY;
      while (enqueued[index]) {
        index = (index + 1) % CAPACITY;
      }
      queue_push_back(&queue, &nodes[index]);
      enqueued[index] = true;
      expected[(head + count) % CAPACITY] = index;
      ++count;
    } else {
      size_t index = expected[head];
      struct queue_node* node = queue_pop_front(&queue);
      assert(node == &nodes[index]);
      assert(node->next == NULL);
      enqueued[index] = false;
      head = (head + 1) % CAPACITY;
      --count;
    }

    assert(queue.size == count);
    assert(empty(&queue) == (count == 0));
    assert((queue.head == NULL) == (count == 0));
    assert((queue.tail == NULL) == (count == 0));
    if (count != 0) {
      assert(queue.head == &nodes[expected[head]]);
      assert(queue.tail->next == NULL);
    }
  }

  while (count != 0) {
    size_t index = expected[head];
    assert(queue_pop_front(&queue) == &nodes[index]);
    head = (head + 1) % CAPACITY;
    --count;
    assert(queue.size == count);
  }
  assert(empty(&queue));
  assert(queue.head == NULL && queue.tail == NULL);
}
