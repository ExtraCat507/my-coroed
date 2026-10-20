#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <time.h>

#include "coroed/api/task.h"

enum { PRIORITY_TASKS = 12, PRIORITY_STEPS = 3 };

struct priority_case {
  uint64_t burst_ms;
  atomic_int steps;
};

static struct priority_case priority_cases[PRIORITY_TASKS];
static atomic_int priority_finished;

static uint64_t priority_cpu_time_ns() {
  struct timespec ts;
  assert(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void priority_burn_cpu(uint64_t ms) {
  uint64_t start = priority_cpu_time_ns();
  while (priority_cpu_time_ns() - start < ms * 1000000ULL) {
  }
}

TASK_DEFINE(priority_load, struct priority_case, argument) {
  for (int i = 0; i < PRIORITY_STEPS; ++i) {
    priority_burn_cpu(argument->burst_ms);
    task_yield(__self);
    atomic_fetch_add(&argument->steps, 1);
  }
  atomic_fetch_add(&priority_finished, 1);
}

void test_priority_load() {
  static const uint64_t durations[] = {3, 12, 25};
  atomic_store(&priority_finished, 0);

  tasks_init();
  for (int i = 0; i < PRIORITY_TASKS; ++i) {
    priority_cases[i].burst_ms = durations[i % 3];
    atomic_store(&priority_cases[i].steps, 0);
    tasks_submit(priority_load, &priority_cases[i]);
  }

  tasks_start();
  tasks_wait();

  assert(atomic_load(&priority_finished) == PRIORITY_TASKS);
  for (int i = 0; i < PRIORITY_TASKS; ++i) {
    assert(atomic_load(&priority_cases[i].steps) == PRIORITY_STEPS);
  }
  tasks_destroy();
}
