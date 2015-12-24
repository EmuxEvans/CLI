#include "libev.h"

struct libev_socket_list {
    int sock;
    libev_sock_type_t socktype;
    void *app_arg;
    void (*accept_func)(void *args);
    void (*recv_func)(void *args);
    void (*send_func)(void *args);
    struct libev_socket_list *next;
};

struct libev_socket_context {
    int max_fd;
    fd_set fds;
    struct libev_socket_list *head, *tail;
};

struct libev_context {
    struct libev_socket_context *sock_context;
};

void* libev_system_init()
{
    struct libev_context *context;

    context = calloc(1, sizeof(struct libev_context));
    if (!context)
        return NULL;

    return context;
}

void libev_system_deinit(void *ctx)
{
    free(ctx);
}

int _libev_register_sock(int sock, void *ctx, void *app_arg, void (*cbfunc)(void *app_arg), libev_sock_type_t socktype)
{
    struct libev_context *context = ctx;
    struct libev_socket_list *sock_node;
    struct libev_socket_context *sock_context;

    sock_context = calloc(1, sizeof(struct libev_socket_context));
    if (!sock_context)
        return -1;

    context->sock_context = sock_context;

    if (sock > sock_context->max_fd)
        sock_context->max_fd = sock;

    FD_SET(sock, &sock_context->fds);

    sock_node = calloc(1, sizeof(struct libev_socket_list));
    if (!sock_node)
        return -1;

    if (!sock_context->head) {
        sock_context->head = sock_node;
        sock_context->tail = sock_node;
    } else {
        sock_context->tail->next = sock_node;
        sock_context->tail = sock_node;
    }

    sock_node->app_arg = app_arg;
    switch (socktype) {
        case LIBEV_SOCK_TYPE_TCP_UNIX:
            sock_node->accept_func = cbfunc;
        break;
        case LIBEV_SOCK_TYPE_TCP_RECV:
            sock_node->recv_func = cbfunc;
        break;        
    }
    sock_node->sock = sock;
    sock_node->socktype = socktype;

    return 0;
}

int libev_register_tcp_unix_sock(int sock, void *ctx, void *app_arg, void (*accept_func)(void *app_arg))
{
    return _libev_register_sock(sock, ctx, app_arg, accept_func, LIBEV_SOCK_TYPE_TCP_UNIX);
}

int libev_register_sock(int sock, void *ctx, void *app_arg, void (*recv_func)(void *app_arg))
{
    return _libev_register_sock(sock, ctx, app_arg, recv_func, LIBEV_SOCK_TYPE_TCP_RECV);
}

static struct libev_socket_list *
libev_get_socket_node_by_sock(int sock, struct libev_context *context)
{
    struct libev_socket_context *sock_context = context->sock_context;
    struct libev_socket_list *sock_node;

    for (sock_node = sock_context->head; sock_node; sock_node = sock_node->next) {
        if (sock_node->sock == sock)
            return sock_node;
    }

    return NULL;
}

void libev_unregister_tcp_unix_sock(int sock, void *ctx)
{
    struct libev_context *context = ctx;
    struct libev_socket_list *node;

    node = libev_get_socket_node_by_sock(sock, context);
    if (!node)
        return;

    FD_CLR(node->sock, &context->sock_context->fds);
}

void libev_accept_func(struct libev_context *context,
                       struct libev_socket_list *sock_node)
{
    sock_node->accept_func(sock_node->app_arg);
}

void libev_recv_func(struct libev_context *context,
                     struct libev_socket_list *sock_node)
{
    sock_node->recv_func(sock_node->app_arg);
}

void libev_sock_event_func(fd_set *allfd, struct libev_context *context)
{
    struct libev_socket_context *sock_context = context->sock_context;
    struct libev_socket_list *sock_node;

    for (sock_node = sock_context->head; sock_node; sock_node = sock_node->next) {
        if (FD_ISSET(sock_node->sock, allfd)) {
            switch (sock_node->socktype) {
                case LIBEV_SOCK_TYPE_TCP_UNIX:
                    libev_accept_func(context, sock_node);
                break;
                case LIBEV_SOCK_TYPE_TCP_RECV:
                    libev_recv_func(context, sock_node);
                break;
            }
            FD_SET(sock_node->sock, &sock_context->fds);
        }
    }
}

void libev_main(void *ctx)
{
    struct libev_context *context = ctx;
    fd_set allfd;
    struct timeval tv = {0, 0};
    int ret;

    for (;;) {
        allfd = context->sock_context->fds;

        ret = select(context->sock_context->max_fd + 1, &allfd, NULL, NULL, &tv);
        if (ret > 0) {
            libev_sock_event_func(&allfd, context);
        } else if (ret == 0) {
        }
    }
}

int libev_unix_tcp_init(void *ctx, char *path, void (*accept_func)(void *args), void *app_arg)
{
    int sock;
    struct sockaddr_un srv;
    struct libev_context *context = ctx;

    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0)
        return -1;

    srv.sun_family = AF_UNIX;
    strcpy(srv.sun_path, path);

    unlink(path);

    if (bind(sock, (struct sockaddr *)&srv, sizeof(srv)) < 0)
        goto err;

    libev_register_tcp_unix_sock(sock, context, app_arg, accept_func);
    return sock;

err:
    close(sock);
    return -1;
}

int libev_unix_tcp_conn(void *ctx, char *path, void (*recv_func)(void *args), void *app_arg)
{
    int sock;
    struct sockaddr_un srv;
    struct libev_context *context = ctx;

    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0)
        return -1;

    srv.sun_family = AF_UNIX;
    strcpy(srv.sun_path, path);

    if (connect(sock, (struct sockaddr *)&srv, sizeof(srv) < 0))
        goto err;

    libev_register_sock(sock, context, app_arg, recv_func);
    return sock;

err:
    close(sock);
    return -1;
}

int libev_create_unix_tcp_conn(char *path)
{
    int sock;
    struct sockaddr_un srv;

    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0)
        return -1;

    srv.sun_family = AF_UNIX;
    strcpy(srv.sun_path, path);

    if (connect(sock, (struct sockaddr *)&srv, sizeof(srv) < 0))
        goto err;

    return sock;

err:
    close(sock);
    return -1;
}

void libev_unix_tcp_deinit(void *ctx, int sock)
{
    close(sock);
    libev_unregister_tcp_unix_sock(sock, ctx);
}