#include "io.h"

#include <liburing.h>
#include <stdio.h>
#include <stdlib.h>

#include "coroed/api/event.h"
#include "coroed/sched/io.h"
#include "coroed/sched/schedy.h"


static int io_submit_and_wait(
    struct task *task,
    struct io_uring *ring,
    struct io_request *request
) {
    int ret = io_uring_submit(ring);

    if (ret <= 0) {
        perror("io_uring_submit failed\n");
        abort();
    }

    event_wait(task, &request->event);

    return request->result;
}



ssize_t coro_recv(struct task *task, int fd, void *buffer, int len){
    struct io_request r = {0};
    r.type = OP_RECV;
    event_init(&r.event);
    struct io_uring *ring = sched_current_ring(task);
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    if(!sqe){
        // full sqe
        perror("SQ full while scheduling recv\n");
        return -ENOBUFS;
    }

    io_uring_prep_recv(sqe, fd, buffer, len, 0);
    io_uring_sqe_set_data(sqe, &r);

    return io_submit_and_wait(task, ring, &r);
}

ssize_t coro_accept(struct task* task, int fd, struct sockaddr *addr, socklen_t *addr_len){
    struct io_request r = {0};
    r.type = OP_ACCEPT;
    event_init(&r.event);
    struct io_uring *ring = sched_current_ring(task);
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    if(!sqe){
        // full sqe
        perror("SQ full while scheduling recv\n");
        return -ENOBUFS;
    }

    io_uring_prep_accept(sqe, fd, addr, addr_len,0);
    io_uring_sqe_set_data(sqe, &r);

    return io_submit_and_wait(task, ring, &r);
}


ssize_t coro_send(struct task *task, int fd, void *buffer, int len){
    struct io_request r = {0};
    r.type = OP_SEND;
    event_init(&r.event);
    struct io_uring *ring = sched_current_ring(task);
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    if(!sqe){
        // full sqe
        perror("SQ full while scheduling recv\n");
        return -ENOBUFS;
    }

    io_uring_prep_send(sqe, fd, buffer, len,0);
    io_uring_sqe_set_data(sqe, &r);

    return io_submit_and_wait(task, ring, &r);
}
