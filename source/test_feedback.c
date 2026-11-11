#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <time.h>

#include "coroed/api/task.h"

static atomic_int completed = 0;

static uint64_t cpu_time_ns() {
  struct timespec ts;
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void burn_cpu_ms(uint64_t ms) {
  uint64_t start = cpu_time_ns();
  while (cpu_time_ns() - start < ms * 1000000ULL) {
  }
}

TASK_DEFINE(feedback, void, argument) {
  uint64_t ms = (uint64_t)(uintptr_t)argument;
  burn_cpu_ms(ms);
  YIELD;
  atomic_fetch_add(&completed, 1);
}

void test_feedback() {
  atomic_store(&completed, 0);
  tasks_init();

  tasks_submit(feedback, (void*)(uintptr_t)3);
  tasks_submit(feedback, (void*)(uintptr_t)12);
  tasks_submit(feedback, (void*)(uintptr_t)25);

  tasks_start();
  tasks_wait();

  assert(atomic_load(&completed) == 3);
  tasks_destroy();
}
