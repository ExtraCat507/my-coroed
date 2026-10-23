#pragma once

#include "coroed/api/event.h"
#include "coroed/api/task.h"
#include "uthread.h"

struct task;
struct event;

void sched_init();

task_t sched_submit(uthread_routine entry, void* argument);

void sched_start();

void sched_wait();

void sched_print_statistics();

void sched_destroy();

void sched_event_wait(struct task* caller, struct event* event);

void sched_event_fire(struct event *event);
