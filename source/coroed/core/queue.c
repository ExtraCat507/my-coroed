#include "queue.h"

#include <stddef.h>
#include <unistd.h>

void queue_init(struct queue *q)
{
    q->head = NULL;
    q->tail = NULL;
    q->size = 0;
}


void queue_push_back(struct queue *queue, struct queue_node *node){
    node->next = NULL;
    queue->size++;
    if(queue->tail == NULL){
        queue->head = node;
        queue->tail = node;
        return;
    }
    queue->tail->next = node;
    queue->tail = node;
}

struct queue_node *queue_pop_front(struct queue *queue){
    struct queue_node *top = queue->head;
    if (empty(queue)){ // empty
        return  NULL;
    }
    if(top->next == NULL){ // one element
        queue_init(queue); // clear the queue
        return top;
    }
    queue->head = top->next;
    top->next = NULL;
    queue->size--;
    return top;
}

bool empty(struct queue *q){
    return q->head == NULL;
}
