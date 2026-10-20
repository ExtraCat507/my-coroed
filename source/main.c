#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "coroed/api/log.h"

struct test {
  const char* name;
  void (*rountine)();
};

void test_counter();
void test_event();
void test_print();
void test_feedback();
void test_queue();
void test_priority_load();
void test_sched_empty();
void test_sched_single();
void test_sched_stress();

int main() {
  log_init();
  srand(time(NULL));

  struct test tests[] = {
      {"counter", test_counter},
      {  "event",   test_event},
      {  "print",   test_print},
      {"feedback", test_feedback},
      {"queue", test_queue},
      {"priority", test_priority_load},
      {"empty", test_sched_empty},
      {"single", test_sched_single},
      {"stress", test_sched_stress},
      {     NULL,         NULL},
  };

  for (struct test* test = tests; test->name != NULL; ++test) {
    printf("Running test '%s'... ", test->name);
    test->rountine();
    printf("Passed!\n");
  }

  return 0;
}
