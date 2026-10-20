#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "coroed/api/task.h"

static atomic_int completed;
static atomic_uint_fast64_t checksum;
static int scenario;

static uint64_t clock_ns(clockid_t clock_id) {
  struct timespec ts;
  int result = clock_gettime(clock_id, &ts);
  assert(result == 0);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

TASK_DEFINE(bench_work, void, argument) {
  uintptr_t index = (uintptr_t)argument;
  uint64_t value = index + 1;
  int steps = scenario == 0 ? 8 : scenario == 1 ? 50 : 2;

  for (int step = 0; step < steps; ++step) {
    if (scenario == 2) {
      uint64_t ms = index % 3 == 0 ? 1 : index % 3 == 1 ? 12 : 25;
      uint64_t start = clock_ns(CLOCK_THREAD_CPUTIME_ID);
      do {
        value = value * 1664525ULL + 1013904223ULL;
      } while (clock_ns(CLOCK_THREAD_CPUTIME_ID) - start < ms * 1000000ULL);
    } else {
      int iterations = scenario == 0 ? 1000 : 10;
      for (int i = 0; i < iterations; ++i) {
        value = value * 1664525ULL + 1013904223ULL;
      }
    }
    task_yield(__self);
  }

  atomic_fetch_xor(&checksum, value);
  atomic_fetch_add(&completed, 1);
}

static void run_benchmark(const char* name, int mode, int count) {
  scenario = mode;
  atomic_store(&completed, 0);
  atomic_store(&checksum, 0);

  tasks_init();
  for (int i = 0; i < count; ++i) {
    tasks_submit(bench_work, (void*)(uintptr_t)i);
  }

  uint64_t start = clock_ns(CLOCK_MONOTONIC);
  tasks_start();
  tasks_wait();
  double elapsed_ms = (clock_ns(CLOCK_MONOTONIC) - start) / 1000000.0;

  assert(atomic_load(&completed) == count);
  printf("bench,%s,%d,%.3f,%.2f,%llu\n", name, count, elapsed_ms,
         count * 1000.0 / elapsed_ms,
         (unsigned long long)atomic_load(&checksum));
  tasks_destroy();
}

void test_benchmark(void) {
  printf("benchmark,scenario,tasks,wall_ms,tasks_per_sec,checksum\n");
  run_benchmark("short", 0, 2000);
  run_benchmark("yield", 1, 500);
  run_benchmark("mixed", 2, 60);
}
