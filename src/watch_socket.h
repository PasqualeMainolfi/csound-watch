#ifndef WATCH_SOCKET_H
#define WATCH_SOCKET_H

#include <stdint.h>

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

typedef SOCKET watch_socket_t;
typedef int watch_socklen_t;

#define WATCH_INVALID_SOCKET INVALID_SOCKET

#else

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

typedef int watch_socket_t;
typedef socklen_t watch_socklen_t;

#define WATCH_INVALID_SOCKET (-1)

#endif

typedef struct {
    struct sockaddr_storage address;
    watch_socklen_t size;
} watch_endpoint_t;

int32_t watch_net_init(void);
void watch_net_cleanup(void);

watch_socket_t watch_socket_udp_bind(const char *address, uint16_t port);
int64_t watch_socket_receive_from(
    watch_socket_t socket_fd,
    void *buffer,
    uint32_t capacity,
    watch_endpoint_t *sender);
int32_t watch_socket_send_to(
    watch_socket_t socket_fd,
    const void *buffer,
    uint32_t size,
    const watch_endpoint_t *destination);
int32_t watch_endpoint_equal(
    const watch_endpoint_t *first,
    const watch_endpoint_t *second);

int32_t watch_socket_close(watch_socket_t socket_fd);
int32_t watch_socket_set_nonblocking(watch_socket_t socket_fd);
int32_t watch_socket_last_error(void);
int32_t watch_socket_would_block(int32_t error_code);

#endif /* WATCH_SOCKET_H */
