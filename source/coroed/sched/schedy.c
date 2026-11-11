#include "schedy.h"

#include <assert.h>
#include <iso646.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "coroed/api/event.h"
#include "coroed/api/task.h"
#include "coroed/core/relax.h"
#include "coroed/core/spinlock.h"
#include "coroed/core/queue.h"
#include "coroed/core/container_of.h"
#include "kthread.h"
#include "uthread.h"

enum {
  /**
   * Максимальное количество файберов, которое может
   * одновременно выполняться на планировщике.
   */
  SCHED_THREADS_LIMIT = 512,

  /**
   * Количество рабочих потоков для исполнения файберов.
   */
  SCHED_WORKERS_COUNT = (size_t)(8),

  /**
   * Обеспечивает костыль для ретрая операций
   * `submit` и `acquire_next` при высокой
   * конкуренции на спинлоках файберов.
   */
  SCHED_NEXT_MAX_ATTEMPTS = (size_t)(16),
};

enum priority {
    PRIORITY_HIGH,
    PRIORITY_MEDIUM,
    PRIORITY_LOW,
    PRIORITY_COUNT
};

/**
 * Задача, выполняющаяся на планировщике.
 *
 * Hint: также тут можно хранить список задач, ожидающих
 *       завершения этой, чтобы уведомить их о данном
 *       событии.
 */
struct task {
  /**
   * Контекст исполнения.
   */
  struct uthread* thread;

  /**
   * Установлен при `UTHREAD_RUNNING`. Указывает на
   * рабочий поток, на котором исполняется задача.
   * Необходим для получения контекста локального
   * планировщика при операциях `yield`, `await`, `submit`.
   */
  struct worker* worker;

  // Приоритет таски
  enum priority priority;

  // Соответствующая задаче нода в очереди
  struct queue_node queue_node;
  struct task* all_next;
  struct timespec run_started_at;
  uint64_t last_burst_ns;

  // Нода задачи в блок-листах других задач
  struct queue_node wait_node;

  // Событие, показываеющее что задача завершилась (одноразовое)
  struct event finished;

  /*  Флаг, показывающий что задача уже может быть разбужена.
    Предотвращает lost wake-up  */
  bool wake_pending;

  /* Флаг, показывающий, что задача может быть безопасно заблокирована */
  bool parked;

  enum {
    /** Готова к исполнению. */
    UTHREAD_RUNNABLE,

    /** Прямо сейчас выполняется. */
    UTHREAD_RUNNING,

    /** Завершена и скоро станет зомби. */
    UTHREAD_FINISHED,

    /** Отработала и может быть переиспользования. */
    UTHREAD_ZOMBIE,

    /** Заблокирована ожиданием другого потока */
    UTHREAD_BLOCKED,
  } state;  // Текущее состояние задачи

  /**
   * Защищает поля структуры от неупорядоченного доступа.
   */
  struct spinlock lock;
};

/**
 * Рабочий поток, исполняющий файберы.
 */
struct worker {
  /**
   * Идентификатор рабочего потока.
   */
  size_t index;

  /**
   * Поток операционной системы, на котором
   * исполняется рабочий.
   */
  struct kthread kthread;

  /**
   * Контекст планировщика. Нужно переключиться на него,
   * чтобы вернуться в планировщик.
   */
  struct uthread sched_thread;

  /**
   * В данный момент исполняемая на рабочем
   * задача. Может быть `NULL`.
   */
  struct task* running_task;

  struct {
    size_t steps;     // Сколько шагов было выполнено
    size_t finished;  // Сколько задач было завершено
  } statistics;       // Локальная статистика работяги
};

static struct spinlock tasks_lock;              // Защищает список задач
// static size_t next_task_index = 0;              // Для планирования round-robin
// static struct task tasks[SCHED_THREADS_LIMIT];  // Список всех задач

// Hint: для реализации более сложных схем управления, вам
//       вам могут понадобиться связаные списки (`core/list.h`). - nahhh я напишу своё

static struct queue multilevel_feedback_queue[PRIORITY_COUNT];
static struct task* all_tasks = NULL;
static size_t active_tasks = 0;

static kthread_id_t kthread_ids[SCHED_WORKERS_COUNT];
static struct worker workers[SCHED_WORKERS_COUNT];
static enum priority pickme_order[] = {PRIORITY_HIGH, PRIORITY_MEDIUM, PRIORITY_HIGH, PRIORITY_LOW, PRIORITY_HIGH, PRIORITY_HIGH, PRIORITY_MEDIUM, PRIORITY_HIGH, PRIORITY_MEDIUM, PRIORITY_HIGH};
static size_t order_pointer = 0;

/**
 * Установить задачу в пустое состояние.
 */
void sched_task_init(struct task* task) {
  task->thread = NULL;
  task->worker = NULL;
  task->state = UTHREAD_ZOMBIE;
  task->priority = PRIORITY_HIGH;
  task->queue_node.next = NULL;
  task->all_next = NULL;
  task->last_burst_ns = 0;
  task->wake_pending = false;
  task->parked = false;
  event_init(&task->finished);
  spinlock_init(&task->lock);
}

/**
 * Установить рабочего в пустое состояние.
 */
void sched_worker_init(struct worker* worker, size_t index) {
  worker->index = index;
  worker->sched_thread.context = NULL;
  worker->running_task = NULL;
  worker->statistics.steps = 0;
  worker->statistics.finished = 0;
}

void sched_init() {
  spinlock_init(&tasks_lock);
  all_tasks = NULL;
  active_tasks = 0;
  for (size_t i = 0; i < PRIORITY_COUNT; ++i) {
    queue_init(&multilevel_feedback_queue[i]);
  }
  for (size_t i = 0; i < SCHED_WORKERS_COUNT; ++i) {
    kthread_ids[i] = 0;
    sched_worker_init(&workers[i], i);
  }
}

/**
 * Находясь в контексте задачи `task`, переключиться
 * в контекст планировщика.
 */
void sched_switch_to_scheduler(struct task* task) {
  struct uthread* sched = &task->worker->sched_thread;
  task->worker = NULL;
  uthread_switch(task->thread, sched);
}

/**
 * Находясь в контексте планировщика, переключиться
 * в контекст задачи `task`. Выполнять ее до следующего
 * невынужденного возвращения в планировщик.
 */
void sched_switch_to(struct worker* worker, struct task* task) {
  assert(task->thread != &worker->sched_thread);

  task->state = UTHREAD_RUNNING;

  task->worker = worker;
  worker->running_task = task;

  struct uthread* sched = &worker->sched_thread;
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &task->run_started_at);
  uthread_switch(sched, task->thread);
  struct timespec ended_at;
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ended_at);
  int64_t elapsed = (int64_t)(ended_at.tv_sec - task->run_started_at.tv_sec) * 1000000000LL
                    + ended_at.tv_nsec - task->run_started_at.tv_nsec;
  task->last_burst_ns = elapsed > 0 ? (uint64_t)elapsed : 0;
}

/**
 * Получить следующую задачу на исполнение в
 * соответствии с текущим алгоритмом планирования.
 *
 * Вызывающему передается владение задачей, а также
 * захваченный лок `task->lock`.
 */
struct task* sched_acquire_next();

/* Отдать сигнал о том, что событие случилось. ИСПОЛЬЗОВАТЬ ТОЛЬКО ПОД ВНЕШНЕЙ БЛОКИРОВКОЙ */
void sched_event_fire_locked(struct event * event);

/**
 * Вернуть задачу в очередь планирования.
 *
 * Очереди передается владение задачей,
 * а также она отпустит лок `task->lock`.
 */
void sched_release(struct task* task);

/**
 * Цикл планировщика. Выполняется, пока есть задачи.
 */
int sched_loop(void* argument) {
  struct worker* worker = argument;
  kthread_ids[worker->index] = kthread_id();

  for (;;) {
    struct task* task = sched_acquire_next();
    if (task == NULL) {
      spinlock_lock(&tasks_lock);
      bool done = active_tasks == 0;
      spinlock_unlock(&tasks_lock);
      if (done) {
        break;
      }
      sched_yield();
      continue;
    }

    sched_switch_to(worker, task);

    worker->statistics.steps += 1;
    if (task->state == UTHREAD_FINISHED) {
      worker->statistics.finished += 1;
    }

    sched_release(task);

    // Hint: где-то здесь можно было бы опросить
    //       механизмы для неблокирующего ввода-вывода
    //       и перевести удовлетворенные BLOCKED
    //       потоки в RUNNABLE состояние.
  }

  return 0;
}

// МОНСТРУОЗНЫЙ TODO
struct task* sched_acquire_next() {
  spinlock_lock(&tasks_lock);

  size_t designated_priority = pickme_order[order_pointer];
  order_pointer = (order_pointer+1) % 10;

  for (size_t offset = 0; offset < PRIORITY_COUNT; ++offset) {
    size_t priority = (designated_priority + offset) % PRIORITY_COUNT;
    struct queue_node* node = queue_pop_front(&multilevel_feedback_queue[priority]);

    if (node == NULL) {
      continue;
    }

    struct task* task = container_of(node, struct task, queue_node);
    spinlock_lock(&task->lock);
    assert(task->thread != NULL && task->state == UTHREAD_RUNNABLE);

    spinlock_unlock(&tasks_lock);
    return task;
  }

  spinlock_unlock(&tasks_lock);
  return NULL;
}

void sched_release(struct task* task) {
  task->worker = NULL;
  if (task->state == UTHREAD_FINISHED) {
    // Отправляем задачу на кладбище, а могли бы
    // еще, например, разблокировать зависимые задачи.
    uthread_reset(task->thread);
    spinlock_lock(&tasks_lock);
    task->state = UTHREAD_ZOMBIE;
    --active_tasks;
    sched_event_fire_locked(&task->finished);
    spinlock_unlock(&task->lock);
    spinlock_unlock(&tasks_lock);
  } else if (task->state == UTHREAD_RUNNING) { // 8ms - high, 9-20ms - medium, >20 low
    task->priority = task->last_burst_ns < 8000000ULL ? PRIORITY_HIGH :
                     task->last_burst_ns < 20000000ULL ? PRIORITY_MEDIUM : PRIORITY_LOW;
    spinlock_lock(&tasks_lock);
    task->state = UTHREAD_RUNNABLE;
    queue_push_back(&multilevel_feedback_queue[task->priority], &task->queue_node);
    spinlock_unlock(&task->lock);
    spinlock_unlock(&tasks_lock);
  } else if (task->state == UTHREAD_BLOCKED)  {
      spinlock_lock(&tasks_lock);

      if (task->wake_pending) {
          task->wake_pending = false;
          task->state = UTHREAD_RUNNABLE;
          task->priority = PRIORITY_HIGH;

          queue_push_back(
              &multilevel_feedback_queue[task->priority],
              &task->queue_node
          );
      }
      else{
          task->parked = true;
      }

      spinlock_unlock(&task->lock);
      spinlock_unlock(&tasks_lock);

  }
}

/**
 * Отметить задачу завершенной.
 */
void sched_finish(struct task* task) {
  task->state = UTHREAD_FINISHED;
}

void task_yield(struct task* caller) {
  sched_switch_to_scheduler(caller);
}

void task_exit(struct task* caller) {
  sched_finish(caller);
  task_yield(caller);
}

task_t task_submit(struct task* caller, uthread_routine entry, void* argument) {
  (void)caller;  // Может быть полезно.
  task_t child = sched_submit(*entry, argument);
  return child;
}

// Попытка сделать таску RUNNABLE с новым контекстом и засабмитить её в очередь (TODO - поддержка Zombie)
task_t sched_try_submit(void (*entry)(), void* argument) {
    struct task* task = malloc(sizeof(*task));

    if (task == NULL)
        return (task_t){.task = NULL};

    sched_task_init(task);
    task->thread = uthread_allocate();
    if (task->thread == NULL) {
        free(task);
        return (task_t){.task = NULL};
    }

    uthread_reset(task->thread);
    uthread_set_entry(task->thread, entry);
    uthread_set_arg_0(task->thread, task);
    uthread_set_arg_1(task->thread, argument);
    task->state = UTHREAD_RUNNABLE;
    task->priority = PRIORITY_HIGH;

    spinlock_lock(&tasks_lock);
    task->all_next = all_tasks;
    all_tasks = task;
    ++active_tasks;
    queue_push_back(&multilevel_feedback_queue[task->priority], &task->queue_node);
    spinlock_unlock(&tasks_lock);

    return (task_t){.task = task};
}

task_t sched_submit(void (*entry)(), void* argument) {
  for (size_t attempt = 0; attempt < SCHED_NEXT_MAX_ATTEMPTS; ++attempt) {
    task_t handle = sched_try_submit(entry, argument);
    if (handle.task != NULL) {
      return handle;
    }
    SPINLOOP(2 * attempt);
  }

  assert(false && "Can't create a task");
}

void sched_start() {
  for (size_t i = 0; i < SCHED_WORKERS_COUNT; ++i) {
    struct worker* worker = &workers[i];
    enum kthread_status status = kthread_create(&worker->kthread, sched_loop, worker);
    assert(status == KTHREAD_SUCCESS);
  }
}

void sched_wait() {
  for (size_t i = 0; i < SCHED_WORKERS_COUNT; ++i) {
    struct worker* worker = &workers[i];
    enum kthread_status status = kthread_join(&worker->kthread);
    assert(status == KTHREAD_SUCCESS);
  }
}

void sched_print_statistics() {
  printf("\nsched statistics\n");

  size_t tasks_count = 0;
  size_t steps_count = 0;
  for (size_t i = 0; i < SCHED_WORKERS_COUNT; ++i) {
    struct worker* worker = &workers[i];
    tasks_count += worker->statistics.finished;
    steps_count += worker->statistics.steps;
  }

  printf("|- tasks executed %zu\n", tasks_count);
  printf("|- steps done     %zu\n", steps_count);

  for (size_t i = 0; i < SCHED_WORKERS_COUNT; ++i) {
    struct worker* worker = &workers[i];
    printf("|- worker %zu %zu\n", i, kthread_ids[i]);
    printf("   |- steps     %zu\n", worker->statistics.steps);
    printf("   |- finished  %zu\n", worker->statistics.finished);
  }
}

void sched_destroy() {
  struct task* task = all_tasks;
  while (task != NULL) {
    struct task* next = task->all_next;
    if (task->thread != NULL) {
      uthread_free(task->thread);
    }
    free(task);
    task = next;
  }
  all_tasks = NULL;
}

// Блокирует поток, если он ждёт ресурс
void sched_event_wait(struct task* caller, struct event* event){
    spinlock_lock(&tasks_lock);

    if(atomic_load(&event->is_fired)){
        spinlock_unlock(&tasks_lock);
        return;
    }

    caller->wake_pending = false;
    caller->parked = false;
    queue_push_back(&event->waiters, &caller->wait_node);
    caller->state = UTHREAD_BLOCKED;
    spinlock_unlock(&tasks_lock);
    sched_switch_to_scheduler(caller);
}


void sched_event_fire_locked(struct event *event){
    atomic_store(&event->is_fired, true);
    while(!empty(&event->waiters)){
        struct task* blocked_task =  container_of(queue_pop_front(&event->waiters), struct task, wait_node);

        if(!blocked_task->parked){
            blocked_task->wake_pending = true;
            continue;
        }

        blocked_task->parked = false;
        blocked_task->state = UTHREAD_RUNNABLE;
        blocked_task->priority = PRIORITY_HIGH;
        queue_push_back(&multilevel_feedback_queue[blocked_task->priority], &blocked_task->queue_node);

    }
}

//
void sched_event_fire(struct event *event){
    spinlock_lock(&tasks_lock);
    sched_event_fire_locked(event);
    spinlock_unlock(&tasks_lock);
}
