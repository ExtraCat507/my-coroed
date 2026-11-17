#include "io.h"

#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#include <assert.h>
#include <iso646.h>
#include <liburing/io_uring.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <liburing.h>

#include "coroed/api/event.h"


void io_poll_cqe(struct io_uring *ring){
    struct io_uring_cqe *cqe;
    int q = 32;
    int ret;
    while(q--){
        ret = io_uring_peek_cqe(ring, &cqe);
        if(ret == -EAGAIN){
            return; //  свободно
        }

        if (ret < 0){
            assert(false && "Cannot get CQE");
            return;
        }

        struct io_request *request = io_uring_cqe_get_data(cqe);
        request->result = cqe->res;
        io_uring_cqe_seen(ring, cqe);

        event_fire(&request->event);
    }
}
