#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "coroed/api/io.h"
#include "coroed/api/task.h"

#define PORT 8080
#define BUFFER_SIZE 4096

enum response_type{
    TEXT, JSON
};

char *form_response(const char *text, enum response_type type) {
    const char *content_type = type == JSON
        ? "application/json"
        : "text/plain";
    const char *format =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s; charset=utf-8\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s";

    size_t body_len = strlen(text);
    int len = snprintf(NULL, 0, format, content_type, body_len, text);
    if (len < 0)
        return NULL;
    char *response = malloc((size_t)len + 1);
    if (!response)
        return NULL;
    snprintf(response, (size_t)len + 1, format, content_type, body_len, text);
    return response;
}

char *route(const char *url) {
    const char *p = url;

    while (*p && *p != '/') p++;
    if (!*p || strncmp(p, "/hello/", 7) != 0)
        return form_response("Not Found", TEXT);

    p += 7;

    char name[256];
    int i = 0;

    while (*p && *p != ' ' && i < 255)
        name[i++] = *p++;

    name[i] = '\0';

    char json[300];
    snprintf(json, sizeof(json), "{\"message\":\"%s\"}", name);

    return form_response(json, JSON);
}

TASK_DEFINE(handle_client, int, arg){
    int client_fd = *arg;
    free(arg);
    char buffer[BUFFER_SIZE];

    ssize_t n = coro_recv(
        __self, client_fd, buffer, sizeof(buffer) - 1
    );

    if (n <= 0) {
        close(client_fd);
        return;
    }

    buffer[n] = '\0';

    printf("Request:\n%s\n", buffer);

    char *response = route(buffer);

    if (response == NULL) {
        close(client_fd);
        return;
    }

    size_t length = strlen(response);
    size_t sent = 0;

    while (sent < length) {
        ssize_t nsend = coro_send(
            __self,
            client_fd,
            (void *)(response + sent),
            (int)(length - sent)
        );

        if (nsend <= 0) {
            fprintf(stderr, "send failed: %zd\n", nsend);
            break;
        }

        sent += (size_t)nsend;
    }
    free(response);
    close(client_fd);
}


TASK_DEFINE(start_server, void, ignored) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0) {
        perror("socket");
        return;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
               &opt, sizeof(opt));

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK)
    };

    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_fd);
        return;
    }

    if (listen(server_fd, 128) < 0) {
        perror("listen");
        close(server_fd);
        return;
    }

    printf("Listening on 127.0.0.1:%d\n", PORT);
    fflush(stdout);

    for (;;) {
        int client_fd = (int)coro_accept(
            __self, server_fd, NULL, NULL
        );

        if (client_fd < 0) {
            fprintf(stderr, "accept failed: %d\n", client_fd);
            continue;
        }

        int *arg = malloc(sizeof(*arg));
        if (!arg) {
            close(client_fd);
            continue;
        }
        *arg = client_fd;

        GO(handle_client, arg);
    }
}



void test_server() {
  printf("\n");
  tasks_init();
  tasks_submit(&start_server, NULL);
  tasks_start();
  tasks_wait();
  tasks_print_statistics();
  tasks_destroy();
}
