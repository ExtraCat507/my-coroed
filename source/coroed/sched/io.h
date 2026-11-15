#pragma once

#include <bits/types.h>
#include <sys/socket.h>
#include <liburing.h>

#include "coroed/api/event.h"

// Тип IO операции
enum op_type {OP_SEND, OP_RECV, OP_READ, OP_WRITE,OP_ACCEPT};

// Структура IO операции.
struct io_request{
    // Тип операции
    enum op_type type;

    // Ивент отвечающий за готовность запроса. Привязывается к заблокированному uthread.
    struct event event;

    ssize_t result;
};

void io_poll_cqe(struct io_uring *ring);
