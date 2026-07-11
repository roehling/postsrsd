/* PostSRSd - Sender Rewriting Scheme daemon for Postfix
 * Copyright 2012-2026 Timo Röhling <timo@gaussglocke.de>
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "endpoint.h"

#include "util.h"

#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#ifndef SO_REUSEPORT
#    define SO_REUSEPORT SO_REUSEADDR
#endif
#ifdef SOCK_CLOEXEC
#    define HAVE_SOCK_CLOEXEC 1
#else
#    define SOCK_CLOEXEC 0
#endif
#ifdef SOCK_NONBLOCK
#    define HAVE_SOCK_NONBLOCK 1
#else
#    define SOCK_NONBLOCK 0
#    include <fcntl.h>
#endif
#define POSTSRSD_SOCKET_LISTEN_QUEUE 16
#define POSTSRSD_MAX_FDS             4

struct endpoint
{
    size_t num_fds;
    int fd[POSTSRSD_MAX_FDS];
    int lock;
    char* path;
};

static bool create_unix_socket(const char* path, endpoint_t* endpoint)
{
    struct sockaddr_un sa;
    if (NULL_OR_EMPTY_STRING(path))
    {
        log_error("expected file path for unix socket");
        return false;
    }
    if (endpoint->num_fds >= POSTSRSD_MAX_FDS)
    {
        log_warn("too many endpoint sockets");
        return true;
    }
    size_t path_len = strlen(path);
    if (path_len >= sizeof(sa.sun_path))
    {
        log_error("file path for unix socket is too long");
        return false;
    }
    endpoint->lock = lock_acquire(path);
    if (endpoint->lock >= 0)
        unlink(path);
    mode_t old_mask = umask(0111);
    int sock = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (sock < 0)
        goto fail;
#if !defined(HAVE_SOCK_NONBLOCK) || !defined(HAVE_SOCK_CLOEXEC)
    int flags = fcntl(sock, F_GETFL);
    if (fcntl(sock, F_SETFL, flags | O_NONBLOCK | O_CLOEXEC) < 0)
        goto fail;
#endif
    sa.sun_family = AF_UNIX;
    memset(sa.sun_path, 0, sizeof(sa.sun_path));
    memcpy(sa.sun_path, path, path_len);
    if (bind(sock, (const struct sockaddr*)&sa,
             offsetof(struct sockaddr_un, sun_path) + path_len)
        < 0)
        goto fail;
    if (listen(sock, POSTSRSD_SOCKET_LISTEN_QUEUE) < 0)
        goto fail;
    endpoint->fd[endpoint->num_fds++] = sock;
    if (endpoint->lock >= 0)
        endpoint->path = strdup(path);
    return true;
fail:
    umask(old_mask);
    log_perror(errno, NULL);
    if (sock >= 0)
        close(sock);
    if (endpoint->lock >= 0)
    {
        lock_release(path, endpoint->lock);
        endpoint->lock = -1;
    }
    return false;
}

static bool create_inet_sockets(char* addr, int family, endpoint_t* endpoint)
{
    const int one = 1;
    struct addrinfo hints, *ai;
    if (endpoint->num_fds >= POSTSRSD_MAX_FDS)
    {
        log_warn("too many endpoint sockets");
        return true;
    }
    memset(&hints, 0, sizeof(struct addrinfo));
    char* node = addr;
    char* service = NULL;
    if (addr[0] == '[')
    {
        node = ++addr;
        while (*addr != ']')
        {
            if (*addr == 0)
            {
                log_error("expected closing ']' in socket address");
                return false;
            }
            ++addr;
        }
        *addr++ = 0;
        if (*addr != ':')
        {
            log_error("expected ':' separator in socket address");
            return false;
        }
        service = ++addr;
    }
    else
    {
        service = strchr(addr, ':');
        if (service)
        {
            *service = 0;
            ++service;
        }
        else
        {
            service = addr;
            node = NULL;
        }
    }
    if (NULL_OR_EMPTY_STRING(service))
    {
        log_error("expected portnumber in socket address");
        return false;
    }
    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM;
    if (node != NULL && strcmp(node, "*") == 0)
    {
        node = NULL;
        hints.ai_flags |= AI_PASSIVE;
    }
    if (node != NULL && strcmp(node, "localhost") == 0)
    {
        node = NULL;
    }
    int err = getaddrinfo(node, service, &hints, &ai);
    if (err != 0)
    {
        log_error("%s", gai_strerror(err));
        return -1;
    }
    int sock = -1, count = 0, free_fds = POSTSRSD_MAX_FDS - endpoint->num_fds;
    for (struct addrinfo* it = ai; it; it = it->ai_next)
    {
        if (free_fds == 0)
            break;
        sock = socket(it->ai_family,
                      it->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
                      it->ai_protocol);
        if (sock < 0)
            goto fail;
#if !defined(HAVE_SOCK_NONBLOCK) || !defined(HAVE_SOCK_CLOEXEC)
        int flags = fcntl(sock, F_GETFL);
        if (fcntl(sock, F_SETFL, flags | O_NONBLOCK | O_CLOEXEC) < 0)
            goto fail;
#endif
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one)) < 0)
            goto fail;
        if (bind(sock, it->ai_addr, it->ai_addrlen) < 0)
            goto fail;
        if (listen(sock, POSTSRSD_SOCKET_LISTEN_QUEUE) < 0)
            goto fail;
        endpoint->fd[endpoint->num_fds++] = sock;
        free_fds--;
        count++;
        continue;
fail:
        err = errno;
        log_perror(err, NULL);
        if (sock >= 0)
            close(sock);
    }
    freeaddrinfo(ai);
    if (count == 0 && err != 0)
        return -1;
    return count;
}

endpoint_t* endpoint_create(const char* s)
{
    endpoint_t* result = (endpoint_t*)malloc(sizeof(struct endpoint));
    if (result == NULL)
    {
        log_error("failed to allocate endpoint socket handle");
        return NULL;
    }
    result->num_fds = 0;
    for (unsigned i = 0; i < result->num_fds; ++i)
        result->fd[i] = -1;
    result->lock = -1;
    result->path = NULL;
    const char* path = NULL;
    if (strncmp(s, "unix:", 5) == 0)
    {
        path = &s[5];
    }
    else if (strncmp(s, "local:", 6) == 0)
    {
        path = &s[6];
    }
    if (path != NULL)
    {
        if (create_unix_socket(path, result))
            return result;
        log_error("failed to create endpoint '%s'", s);
        endpoint_destroy(result);
        return NULL;
    }
    char* addr = NULL;
    int family = AF_UNSPEC;
    if (strncmp(s, "inet:", 5) == 0)
    {
        addr = strdup(&s[5]);
    }
    else if (strncmp(s, "inet4:", 6) == 0)
    {
        addr = strdup(&s[6]);
        family = AF_INET;
    }
    else if (strncmp(s, "inet6:", 6) == 0)
    {
        addr = strdup(&s[6]);
        family = AF_INET6;
    }
    if (addr != NULL)
    {
        bool ok = create_inet_sockets(addr, family, result);
        free(addr);
        if (ok)
            return result;
        log_error("failed to create endpoint '%s'", s);
        endpoint_destroy(result);
        return NULL;
    }
    log_error("unsupported endpoint '%s'", s);
    endpoint_destroy(result);
    return NULL;
}

void endpoint_destroy(endpoint_t* endpoint)
{
    if (endpoint == NULL)
        return;
    if (endpoint->lock >= 0 && endpoint->path != NULL)
    {
        lock_release(endpoint->path, endpoint->lock);
    }
    if (endpoint->path != NULL)
    {
        unlink(endpoint->path);
        free(endpoint->path);
    }
    for (unsigned i = 0; i < endpoint->num_fds; ++i)
    {
        int sock = endpoint->fd[i];
        if (sock >= 0)
            close(sock);
    }
    free(endpoint);
}

void endpoint_release(endpoint_t* endpoint)
{
    if (endpoint == NULL)
        return;
    if (endpoint->path != NULL)
        free(endpoint->path);
    free(endpoint);
}

size_t endpoint_prepare_poll(endpoint_t* endpoint, struct pollfd* pollfds,
                             size_t max_fds)
{
    if (endpoint == NULL)
        return 0;
    size_t count = 0;
    for (size_t i = 0; i < endpoint->num_fds && i < max_fds; ++i)
    {
        pollfds[i].fd = endpoint->fd[i];
        pollfds[i].events = POLLIN;
        pollfds[i].revents = 0;
        ++count;
    }
    return count;
}
