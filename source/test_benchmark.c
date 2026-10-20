#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "coroed/api/task.h"

enum { BENCH_RUNS = 20, BENCH_WARMUPS = 2, BENCH_MAX_TASKS = 256 };
enum { BENCH_SHORT, BENCH_YIELD, BENCH_MIXED };

struct measurement {
  double wall_ms;
  double tasks_per_sec;
  double short_p50_ms;
  double short_p95_ms;
};

static atomic_int completed;
static atomic_uint_fast64_t checksum;
static atomic_uint_fast64_t work_sink;
static uint64_t short_finish_ns[BENCH_MAX_TASKS];
static uint64_t run_started_ns;
static int scenario;

static uint64_t clock_ns(clockid_t clock_id) {
  struct timespec ts;
  assert(clock_gettime(clock_id, &ts) == 0);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static uint64_t task_checksum(uint64_t index, int mode) {
  uint64_t value = (index + 1) ^ ((uint64_t)(mode + 1) * 0x9e3779b97f4a7c15ULL);
  value ^= value >> 30;
  value *= 0xbf58476d1ce4e5b9ULL;
  value ^= value >> 27;
  value *= 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

TASK_DEFINE(bench_work, void, argument) {
  uint64_t index = (uintptr_t)argument;
  uint64_t value = index + 1;
  int steps = scenario == BENCH_SHORT ? 8 : scenario == BENCH_YIELD ? 50 : 2;

  for (int step = 0; step < steps; ++step) {
    if (scenario == BENCH_MIXED) {
      uint64_t ms = index % 3 == 0 ? 1 : index % 3 == 1 ? 12 : 25;
      uint64_t start = clock_ns(CLOCK_THREAD_CPUTIME_ID);
      do {
        value = value * 1664525ULL + 1013904223ULL;
      } while (clock_ns(CLOCK_THREAD_CPUTIME_ID) - start < ms * 1000000ULL);
    } else {
      int iterations = scenario == BENCH_SHORT ? 1000 : 10;
      for (int i = 0; i < iterations; ++i) {
        value = value * 1664525ULL + 1013904223ULL;
      }
    }
    task_yield(__self);
  }

  // Фиксируем фактическую работу, но не включаем в checksum число
  // итераций CPU burst: оно зависит от скорости процессора и расписания.
  atomic_fetch_xor(&work_sink, value);
  atomic_fetch_xor(&checksum, task_checksum(index, scenario));
  if (scenario == BENCH_MIXED && index % 3 == 0) {
    short_finish_ns[index] = clock_ns(CLOCK_MONOTONIC) - run_started_ns;
  }
  atomic_fetch_add(&completed, 1);
}

static int compare_u64(const void *a, const void *b) {
  uint64_t x = *(const uint64_t *)a;
  uint64_t y = *(const uint64_t *)b;
  return (x > y) - (x < y);
}

static int compare_double(const void *a, const void *b) {
  double x = *(const double *)a;
  double y = *(const double *)b;
  return (x > y) - (x < y);
}

static double median_sorted(const double *values, int count) {
  return count % 2 ? values[count / 2] :
         (values[count / 2 - 1] + values[count / 2]) / 2.0;
}

static struct measurement run_once(int mode, int count, uint64_t *result_checksum) {
  assert(count > 0 && count <= BENCH_MAX_TASKS);
  scenario = mode;
  atomic_store(&completed, 0);
  atomic_store(&checksum, 0);
  atomic_store(&work_sink, 0);

  tasks_init();
  for (int i = 0; i < count; ++i) {
    tasks_submit(bench_work, (void *)(uintptr_t)i);
  }

  run_started_ns = clock_ns(CLOCK_MONOTONIC);
  tasks_start();
  tasks_wait();
  uint64_t elapsed_ns = clock_ns(CLOCK_MONOTONIC) - run_started_ns;

  assert(atomic_load(&completed) == count);
  uint64_t expected = 0;
  for (int i = 0; i < count; ++i) {
    expected ^= task_checksum((uint64_t)i, mode);
  }
  *result_checksum = atomic_load(&checksum);
  assert(*result_checksum == expected);

  struct measurement result = {
      .wall_ms = elapsed_ns / 1000000.0,
      .tasks_per_sec = count * 1000000000.0 / elapsed_ns,
      .short_p50_ms = 0,
      .short_p95_ms = 0,
  };

  if (mode == BENCH_MIXED) {
    uint64_t latencies[BENCH_MAX_TASKS];
    int n = 0;
    for (int i = 0; i < count; i += 3) {
      latencies[n++] = short_finish_ns[i];
    }
    qsort(latencies, (size_t)n, sizeof(latencies[0]), compare_u64);
    result.short_p50_ms = n % 2 ? latencies[n / 2] / 1000000.0 :
        (latencies[n / 2 - 1] / 1000000.0 + latencies[n / 2] / 1000000.0) / 2.0;
    result.short_p95_ms = latencies[(95 * n + 99) / 100 - 1] / 1000000.0;
  }

  tasks_destroy();
  return result;
}

static void benchmark_scenario(const char *name, int mode, int count) {
  uint64_t checksum_value = 0;
  for (int i = 0; i < BENCH_WARMUPS; ++i) {
    (void)run_once(mode, count, &checksum_value);
  }

  double wall[BENCH_RUNS], throughput[BENCH_RUNS];
  double p50[BENCH_RUNS], p95[BENCH_RUNS];
  for (int i = 0; i < BENCH_RUNS; ++i) {
    struct measurement m = run_once(mode, count, &checksum_value);
    wall[i] = m.wall_ms;
    throughput[i] = m.tasks_per_sec;
    p50[i] = m.short_p50_ms;
    p95[i] = m.short_p95_ms;
    if (mode == BENCH_MIXED) {
      printf("bench_run,%s,%d,%d,%.3f,%.2f,%llu,%.3f,%.3f\n",
             name, i + 1, count, m.wall_ms, m.tasks_per_sec,
             (unsigned long long)checksum_value, m.short_p50_ms, m.short_p95_ms);
    } else {
      printf("bench_run,%s,%d,%d,%.3f,%.2f,%llu,,\n",
             name, i + 1, count, m.wall_ms, m.tasks_per_sec,
             (unsigned long long)checksum_value);
    }
  }

  qsort(wall, BENCH_RUNS, sizeof(wall[0]), compare_double);
  qsort(throughput, BENCH_RUNS, sizeof(throughput[0]), compare_double);
  qsort(p50, BENCH_RUNS, sizeof(p50[0]), compare_double);
  qsort(p95, BENCH_RUNS, sizeof(p95[0]), compare_double);
  if (mode == BENCH_MIXED) {
    printf("bench_median,%s,0,%d,%.3f,%.2f,%llu,%.3f,%.3f\n",
           name, count, median_sorted(wall, BENCH_RUNS),
           median_sorted(throughput, BENCH_RUNS),
           (unsigned long long)checksum_value,
           median_sorted(p50, BENCH_RUNS), median_sorted(p95, BENCH_RUNS));
  } else {
    printf("bench_median,%s,0,%d,%.3f,%.2f,%llu,,\n",
           name, count, median_sorted(wall, BENCH_RUNS),
           median_sorted(throughput, BENCH_RUNS),
           (unsigned long long)checksum_value);
  }
}

void test_benchmark(void) {
  printf("type,scenario,run,tasks,wall_ms,tasks_per_sec,checksum,short_p50_ms,short_p95_ms\n");
  benchmark_scenario("short", BENCH_SHORT, 256);
  benchmark_scenario("yield", BENCH_YIELD, 256);
  benchmark_scenario("mixed", BENCH_MIXED, 60);
}
