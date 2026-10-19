#pragma once

#include <stdbool.h>
struct queue_node {
    struct queue_node *next;
};

struct queue {
    struct queue_node *head;
    struct queue_node *tail;
};

// Инициализировать / очистить очередь
void queue_init(struct queue *q);
void queue_push_back(struct queue *q, struct queue_node *node);
struct queue_node *queue_pop_front(struct queue *q);
bool empty(struct queue *q);
