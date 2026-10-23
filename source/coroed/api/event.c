#include "event.h"

#include <stdatomic.h>
#include <stdbool.h>

#include "task.h"
#include "coroed/sched/schedy.h"

void event_init(struct event* event) {
  atomic_store(&event->is_fired, false);
}

void event_wait(struct task* caller, struct event* event) {
  shed_event_wait(caller, event);
}

void event_fire(struct event* event) {
  shed_event_fire(event);
}
