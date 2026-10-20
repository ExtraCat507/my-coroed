#include <assert.h>
#include <stdatomic.h>
#include <stddef.h>

#include "coroed/api/task.h"

enum {
  STRESS_PARENTS = 40,
  STRESS_CHILDREN = 12,
  STRESS_YIELDS = 6,
  STRESS_REPEATS = 5,
};

static atomic_int leaves_started;
static atomic_int leaves_finished;
static atomic_int parents_finished;
static atomic_int executed_steps;
static atomic_int single_steps;

TASK_DEFINE(stress_leaf, void, ignored) {
  (void)ignored;
  atomic_fetch_add(&leaves_started, 1);
  for (int i = 0; i < STRESS_YIELDS; ++i) {
    atomic_fetch_add(&executed_steps, 1);
    task_yield(__self);
  }
  atomic_fetch_add(&leaves_finished, 1);
}

TASK_DEFINE(stress_parent, void, ignored) {
  (void)ignored;
  for (int i = 0; i < STRESS_CHILDREN; ++i) {
    GO(stress_leaf, NULL);
    if (i % 3 == 0) {
      task_yield(__self);
    }
  }
  atomic_fetch_add(&parents_finished, 1);
}

TASK_DEFINE(stress_single, void, ignored) {
  (void)ignored;
  for (int i = 0; i < 128; ++i) {
    atomic_fetch_add(&single_steps, 1);
    task_yield(__self);
  }
}

void test_sched_empty() {
  for (int repeat = 0; repeat < 3; ++repeat) {
    tasks_init();
    tasks_start();
    tasks_wait();
    tasks_destroy();
  }
}

void test_sched_single() {
  for (int repeat = 0; repeat < 3; ++repeat) {
    atomic_store(&single_steps, 0);
    tasks_init();
    tasks_submit(stress_single, NULL);
    tasks_start();
    tasks_wait();
    assert(atomic_load(&single_steps) == 128);
    tasks_destroy();
  }
}

void test_sched_stress() {
  for (int repeat = 0; repeat < STRESS_REPEATS; ++repeat) {
    atomic_store(&leaves_started, 0);
    atomic_store(&leaves_finished, 0);
    atomic_store(&parents_finished, 0);
    atomic_store(&executed_steps, 0);

    tasks_init();
    for (int i = 0; i < STRESS_PARENTS; ++i) {
      tasks_submit(stress_parent, NULL);
    }
    tasks_start();
    tasks_wait();

    assert(atomic_load(&parents_finished) == STRESS_PARENTS);
    assert(atomic_load(&leaves_started) == STRESS_PARENTS * STRESS_CHILDREN);
    assert(atomic_load(&leaves_finished) == STRESS_PARENTS * STRESS_CHILDREN);
    assert(atomic_load(&executed_steps) ==
           STRESS_PARENTS * STRESS_CHILDREN * STRESS_YIELDS);

    tasks_destroy();
  }
}
