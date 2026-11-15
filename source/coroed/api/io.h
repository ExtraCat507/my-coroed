#pragma once


#include <stddef.h>
#include <sys/socket.h>
#include <sys/types.h>
struct task;

ssize_t coro_recv(struct task *task, int fd, void *buffer, int len);

ssize_t coro_accept(struct task* task, int fd, struct sockaddr *addr, socklen_t *addr_len);

ssize_t coro_send(struct task *task, int fd, void *buffer, int len);
