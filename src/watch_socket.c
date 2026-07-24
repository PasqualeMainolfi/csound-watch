#include "watch_socket.h"

#include <limits.h>
#include <string.h>

static void watch_socket_set_error(int32_t error_code) {
#if defined(_WIN32)
    WSASetLastError(error_code);
#else
    errno = error_code;
#endif
}

int32_t watch_net_init(void) {
#if defined(_WIN32)
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0 ? 0 : -1;
#else
    return 0;
#endif
}

void watch_net_cleanup(void) {
#if defined(_WIN32)
    WSACleanup();
#endif
}

watch_socket_t watch_socket_udp_bind(const char *address, uint16_t port) {
    if (address == NULL) {
        watch_socket_set_error(
#if defined(_WIN32)
            WSAEINVAL
#else
            EINVAL
#endif
        );
        return WATCH_INVALID_SOCKET;
    }

    watch_socket_t socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd == WATCH_INVALID_SOCKET) {
        return WATCH_INVALID_SOCKET;
    }

    struct sockaddr_in local_address;
    memset(&local_address, 0, sizeof(local_address));
    local_address.sin_family = AF_INET;
    local_address.sin_port = htons(port);

    if (inet_pton(AF_INET, address, &local_address.sin_addr) != 1) {
        watch_socket_close(socket_fd);
        watch_socket_set_error(
#if defined(_WIN32)
        WSAEINVAL
#else
        EINVAL
#endif
        );
        return WATCH_INVALID_SOCKET;
    }

    if (bind(
            socket_fd,
            (const struct sockaddr *) &local_address,
            (watch_socklen_t) sizeof(local_address)) != 0) {
        int32_t error_code = watch_socket_last_error();
        watch_socket_close(socket_fd);
        watch_socket_set_error(error_code);
        return WATCH_INVALID_SOCKET;
    }

    if (watch_socket_set_nonblocking(socket_fd) != 0) {
        int32_t error_code = watch_socket_last_error();
        watch_socket_close(socket_fd);
        watch_socket_set_error(error_code);
        return WATCH_INVALID_SOCKET;
    }

    return socket_fd;
}

int64_t watch_socket_receive_from(
    watch_socket_t socket_fd,
    void *buffer,
    uint32_t capacity,
    watch_endpoint_t *sender
) {
    if (buffer == NULL || sender == NULL || capacity == 0U || capacity > (uint32_t) INT_MAX) {
        watch_socket_set_error(
#if defined(_WIN32)
            WSAEINVAL
#else
            EINVAL
#endif
        );
        return -1;
    }

    memset(sender, 0, sizeof(*sender));
    sender->size = (watch_socklen_t) sizeof(sender->address);

#if defined(_WIN32)
    int received = recvfrom(
        socket_fd,
        (char *) buffer,
        (int) capacity,
        0,
        (struct sockaddr *) &sender->address,
        &sender->size);
    return received == SOCKET_ERROR ? -1 : (int64_t) received;
#else
    ssize_t received = recvfrom(
        socket_fd,
        buffer,
        (size_t) capacity,
        0,
        (struct sockaddr *) &sender->address,
        &sender->size);
    return (int64_t) received;
#endif
}

int32_t watch_socket_send_to(
    watch_socket_t socket_fd,
    const void *buffer,
    uint32_t size,
    const watch_endpoint_t *destination
) {
    if (buffer == NULL || destination == NULL || size == 0U || size > (uint32_t) INT_MAX) {
        watch_socket_set_error(
#if defined(_WIN32)
            WSAEINVAL
#else
            EINVAL
#endif
        );
        return -1;
    }

#if defined(_WIN32)
    int sent = sendto(
        socket_fd,
        (const char *) buffer,
        (int) size,
        0,
        (const struct sockaddr *) &destination->address,
        destination->size);
    return sent == (int) size ? 0 : -1;
#else
    ssize_t sent = sendto(
        socket_fd,
        buffer,
        (size_t) size,
        0,
        (const struct sockaddr *) &destination->address,
        destination->size);
    return sent == (ssize_t) size ? 0 : -1;
#endif
}

int32_t watch_endpoint_equal(
    const watch_endpoint_t *first,
    const watch_endpoint_t *second
) {
    if (first == NULL || second == NULL) {
        return 0;
    }

    const struct sockaddr *first_address =
        (const struct sockaddr *) &first->address;
    const struct sockaddr *second_address =
        (const struct sockaddr *) &second->address;
    if (first_address->sa_family != second_address->sa_family) {
        return 0;
    }

    if (first_address->sa_family == AF_INET) {
        const struct sockaddr_in *first_ipv4 =
            (const struct sockaddr_in *) first_address;
        const struct sockaddr_in *second_ipv4 =
            (const struct sockaddr_in *) second_address;
        return first_ipv4->sin_port == second_ipv4->sin_port
            && first_ipv4->sin_addr.s_addr == second_ipv4->sin_addr.s_addr;
    }

    if (first_address->sa_family == AF_INET6) {
        const struct sockaddr_in6 *first_ipv6 =
            (const struct sockaddr_in6 *) first_address;
        const struct sockaddr_in6 *second_ipv6 =
            (const struct sockaddr_in6 *) second_address;
        return first_ipv6->sin6_port == second_ipv6->sin6_port
            && first_ipv6->sin6_scope_id == second_ipv6->sin6_scope_id
            && memcmp(
                &first_ipv6->sin6_addr,
                &second_ipv6->sin6_addr,
                sizeof(first_ipv6->sin6_addr)) == 0;
    }

    return 0;
}

int32_t watch_socket_close(watch_socket_t socket_fd) {
#if defined(_WIN32)
    return closesocket(socket_fd) == 0 ? 0 : -1;
#else
    return close(socket_fd);
#endif
}

int32_t watch_socket_set_nonblocking(watch_socket_t socket_fd) {
#if defined(_WIN32)
    u_long enabled = 1;
    return ioctlsocket(socket_fd, FIONBIO, &enabled) == 0 ? 0 : -1;
#else
    int flags = fcntl(socket_fd, F_GETFL, 0);

    if (flags == -1) {
        return -1;
    }

    if (fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        return -1;
    }

    int descriptor_flags = fcntl(socket_fd, F_GETFD, 0);
    if (descriptor_flags == -1) {
        return -1;
    }

    return fcntl(socket_fd, F_SETFD, descriptor_flags | FD_CLOEXEC);
#endif
}

int32_t watch_socket_last_error(void) {
#if defined(_WIN32)
    return (int32_t) WSAGetLastError();
#else
    return errno;
#endif
}

int32_t watch_socket_would_block(int32_t error_code) {
#if defined(_WIN32)
    return error_code == WSAEWOULDBLOCK;
#else
    return error_code == EAGAIN || error_code == EWOULDBLOCK;
#endif
}
