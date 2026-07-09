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
#include "util.h"

#include "postsrsd_build_config.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#ifdef HAVE_ERRNO_H
#    include <errno.h>
#endif
#ifdef HAVE_FCNTL_H
#    include <fcntl.h>
#endif
#ifdef HAVE_POLL_H
#    include <poll.h>
#endif
#ifdef HAVE_SIGNAL_H
#    include <signal.h>
#endif
#ifdef HAVE_SYS_FILE_H
#    include <sys/file.h>
#endif
#ifdef HAVE_SYS_INOTIFY_H
#    include <sys/inotify.h>
#endif
#ifdef HAVE_SYS_MMAN_H
#    include <sys/mman.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#    include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
#    include <sys/stat.h>
#endif
#ifdef HAVE_SYSLOG_H
#    include <syslog.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#    include <sys/socket.h>
#endif
#ifdef HAVE_SYS_TIME_H
#    include <sys/time.h>
#endif
#ifdef HAVE_SYS_UN_H
#    include <sys/un.h>
#endif
#ifdef HAVE_TIME_H
#    include <time.h>
#endif
#ifdef HAVE_UNISTD_H
#    include <unistd.h>
#endif

#ifndef HAVE_STRNCASECMP
#    ifdef HAVE__STRNICMP
#        define strncasecmp _strnicmp
#    endif
#endif
#ifndef O_CLOEXEC
#    define O_CLOEXEC 0
#endif

#ifdef WITH_SECCOMP
#    include <seccomp.h>
#endif

bool string_equal(const void* s1, const void* s2)
{
    return strcmp(s1, s2) == 0;
}

void string_set(char** var, char* value)
{
    free(*var);
    *var = value;
}

#ifndef HAVE_STPNCPY
char* stpncpy(char* dst, const char* src, size_t len)
{
    size_t n = strlen(src);
    if (n > len)
        n = len;
    return strncpy(dst, src, len) + n;
}
#endif

#ifndef HAVE_STPCPY
char* stpcpy(char* dst, const char* src)
{
    size_t n = strlen(src);
    memcpy(dst, src, n + 1);
    return dst + n;
}
#endif

char* b32h_encode(const char* data, size_t length, char* buffer, size_t bufsize)
{
    static const char B32H_CHARS[32] = {'0', '1', '2', '3', '4', '5', '6', '7',
                                        '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
                                        'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N',
                                        'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V'};
    if (data == NULL)
        return NULL;
    if ((bufsize <= 8 * ((length + 4) / 5)))
        return NULL;
    char* out = buffer;
    size_t i, j;
    for (i = 0, j = 0; i + 4 < length; i += 5, j += 8)
    {
        uint64_t tmp = (((uint64_t)data[i] & 0xFF) << 32)
                       | (((uint64_t)data[i + 1] & 0xFF) << 24)
                       | (((uint64_t)data[i + 2] & 0xFF) << 16)
                       | (((uint64_t)data[i + 3] & 0xFF) << 8)
                       | ((uint64_t)data[i + 4] & 0xFF);
        out[j + 7] = B32H_CHARS[tmp & 0x1F];
        tmp >>= 5;
        out[j + 6] = B32H_CHARS[tmp & 0x1F];
        tmp >>= 5;
        out[j + 5] = B32H_CHARS[tmp & 0x1F];
        tmp >>= 5;
        out[j + 4] = B32H_CHARS[tmp & 0x1F];
        tmp >>= 5;
        out[j + 3] = B32H_CHARS[tmp & 0x1F];
        tmp >>= 5;
        out[j + 2] = B32H_CHARS[tmp & 0x1F];
        tmp >>= 5;
        out[j + 1] = B32H_CHARS[tmp & 0x1F];
        tmp >>= 5;
        out[j] = B32H_CHARS[tmp & 0x1F];
    }
    if (i < length)
    {
        uint64_t tmp = (uint64_t)data[i];
        tmp <<= 8;
        if (i + 1 < length)
            tmp |= (uint64_t)data[i + 1];
        tmp <<= 8;
        if (i + 2 < length)
            tmp |= (uint64_t)data[i + 2];
        tmp <<= 8;
        if (i + 3 < length)
            tmp |= (uint64_t)data[i + 3];
        out[j + 7] = '=';
        tmp <<= 3;
        out[j + 6] = i + 3 < length ? B32H_CHARS[tmp & 0x1F] : '=';
        tmp >>= 5;
        out[j + 5] = i + 3 < length ? B32H_CHARS[tmp & 0x1F] : '=';
        tmp >>= 5;
        out[j + 4] = i + 2 < length ? B32H_CHARS[tmp & 0x1F] : '=';
        tmp >>= 5;
        out[j + 3] = i + 1 < length ? B32H_CHARS[tmp & 0x1F] : '=';
        tmp >>= 5;
        out[j + 2] = i + 1 < length ? B32H_CHARS[tmp & 0x1F] : '=';
        tmp >>= 5;
        out[j + 1] = B32H_CHARS[tmp & 0x1F];
        tmp >>= 5;
        out[j] = B32H_CHARS[tmp & 0x1F];
        j += 8;
    }
    out[j] = 0;
    return buffer;
}

bool file_exists(const char* filename)
{
    struct stat st;
    if (stat(filename, &st) < 0)
        return false;
    return S_ISREG(st.st_mode);
}

bool directory_exists(const char* dirname)
{
    struct stat st;
    if (stat(dirname, &st) < 0)
        return false;
    return S_ISDIR(st.st_mode);
}

int lock_acquire(const char* path)
{
    MAYBE_UNUSED(path);
#if defined(LOCK_EX) && defined(LOCK_NB)
    size_t len = strlen(path);
    if (len > SIZE_MAX - 6)
        return -1;
    char* lock_path = malloc(len + 6); /* ".lock" + "\0" */
    if (lock_path == NULL)
        return -1;
    stpcpy(stpcpy(lock_path, path), ".lock");
    int fd = open(lock_path, O_RDONLY | O_CREAT | O_CLOEXEC, 0600);
    free(lock_path);
    if (fd < 0)
        return -1;
    if (flock(fd, LOCK_EX | LOCK_NB) < 0)
    {
        close(fd);
        return -1;
    }
    return fd;
#else
    return 0;
#endif
}

void lock_release(const char* path, int fd)
{
    MAYBE_UNUSED(path);
    MAYBE_UNUSED(fd);
#if defined(LOCK_EX) && defined(LOCK_NB)
    size_t len = strlen(path);
    if (len <= SIZE_MAX - 6)
    {
        char* lock_path = malloc(len + 6); /* ".lock" + "\0" */
        if (lock_path != NULL)
        {
            stpcpy(stpcpy(lock_path, path), ".lock");
            unlink(lock_path);
            free(lock_path);
        }
    }
    close(fd);
#endif
}

/* The domain set is implemented as a common prefix tree */
struct domain_set
{
    /* Membership; is the string up to this point a set member? */
    bool m;
    /* Child node for subdomain components, effectively (".") */
    struct domain_set* s;
    /* Child nodes for letters A-Z, numbers 0-9, and dash ("-"),
       in that order */
    struct domain_set* c[37];
};

#define DOMAIN_SET_ADD           1
#define DOMAIN_SET_PARENTS_MATCH 2

domain_set_t* domain_set_create()
{
    domain_set_t* D = malloc(sizeof(struct domain_set));
    for (unsigned i = 0; i < sizeof(D->c) / sizeof(D->c[0]); ++i)
        D->c[i] = NULL;
    D->s = NULL;
    D->m = false;
    return D;
}

void domain_set_destroy(domain_set_t* D)
{
    for (unsigned i = 0; i < sizeof(D->c) / sizeof(D->c[0]); ++i)
        if (D->c[i])
            domain_set_destroy(D->c[i]);
    if (D->s != NULL)
        domain_set_destroy(D->s);
    free(D);
}

static bool walk_domain_set(domain_set_t* D, char* domain, int flags)
{
    char* dot = strrchr(domain, '.');
    char* subdomain = domain;
    if (dot != NULL)
    {
        subdomain = dot + 1;
        *dot = 0;
    }
    int ch;
    while ((ch = *subdomain++))
    {
        if (ch >= 'A' && ch <= 'Z')
            ch -= 'A';
        else if (ch >= 'a' && ch <= 'z')
            ch -= 'a';
        else if (ch >= '0' && ch <= '9')
            ch = ch - '0' + 26;
        else if (ch == '-')
            ch = 36;
        else
            return 0;
        if (D->c[ch] == NULL)
        {
            if (!(flags & DOMAIN_SET_ADD))
                return 0;
            D->c[ch] = domain_set_create();
        }
        D = D->c[ch];
    }
    if (dot != NULL)
    {
        if (D->s == NULL)
        {
            if (!(flags & DOMAIN_SET_ADD))
                return 0;
            D->s = domain_set_create();
        }
        if (D->s->m && (flags & DOMAIN_SET_PARENTS_MATCH))
            return 1;
        return walk_domain_set(D->s, domain, flags);
    }
    bool result = D->m;
    if (flags == DOMAIN_SET_ADD)
        D->m = true;
    return result;
}

bool domain_set_add(domain_set_t* D, const char* domain)
{
    if (D == NULL)
        return false;
    char buffer[512];
    char* end = stpncpy(buffer, domain, sizeof(buffer) - 1);
    if (end - buffer >= (ptrdiff_t)sizeof(buffer) - 1)
        return false;
    return !walk_domain_set(D, buffer, DOMAIN_SET_ADD);
}

bool domain_set_contains(domain_set_t* D, const char* domain)
{
    if (D == NULL)
        return false;
    char buffer[512];
    char* end = stpncpy(buffer, domain, sizeof(buffer) - 1);
    if (end - buffer >= (ptrdiff_t)sizeof(buffer) - 1)
        return false;
    return walk_domain_set(D, buffer, DOMAIN_SET_PARENTS_MATCH);
}

struct pid_set
{
    size_t capacity;
    size_t size;
    pid_t* entries;
};

pid_set_t* pid_set_create()
{
    pid_set_t* P = malloc(sizeof(pid_set_t));
    if (P != NULL)
    {
        P->capacity = 0;
        P->size = 0;
        P->entries = NULL;
    }
    return P;
}

bool pid_set_add(pid_set_t* P, pid_t pid)
{
    if (P == NULL)
        return false;
    if (pid <= 0)
        return false;
    for (size_t i = 0; i < P->size; ++i)
    {
        if (P->entries[i] == pid)
            return false;
    }
    if (P->capacity == P->size)
    {
        if (P->capacity == 0)
        {
            P->entries = malloc(16 * sizeof(pid_t));
            if (P->entries == NULL)
                return false;
            P->capacity = 4;
        }
        else
        {
            pid_t* new_entries =
                realloc(P->entries, 2 * P->capacity * sizeof(pid_t));
            if (new_entries == NULL)
                return false;
            P->entries = new_entries;
            P->capacity *= 2;
        }
    }
    P->entries[P->size++] = pid;
    return true;
}

bool pid_set_remove(pid_set_t* P, pid_t pid)
{
    if (P == NULL)
        return false;
    for (size_t i = 0; i < P->size; ++i)
    {
        if (P->entries[i] == pid)
        {
            --P->size;
            P->entries[i] = P->entries[P->size];
            return true;
        }
    }
    return false;
}

bool pid_set_kill(pid_set_t* P, int signal)
{
    if (P == NULL)
        return false;
    for (size_t i = 0; i < P->size;)
    {
        if (kill(P->entries[i], signal) < 0)
        {
            if (errno == EINVAL)
                return false;
            if (errno == ESRCH || errno == EPERM)
            {
                --P->size;
                P->entries[i] = P->entries[P->size];
                continue;
            }
        }
        ++i;
    }
    return true;
}

void pid_set_destroy(pid_set_t* P)
{
    if (P == NULL)
        return;
    free(P->entries);
    free(P);
}
struct list
{
    size_t capacity;
    size_t size;
    void** entries;
};

list_t* list_create()
{
    list_t* L = malloc(sizeof(list_t));
    if (L != NULL)
    {
        L->capacity = 0;
        L->size = 0;
        L->entries = NULL;
    }
    return L;
}

void* list_get(list_t* L, size_t i)
{
    if (L != NULL && i < L->size)
        return L->entries[i];
    return NULL;
}

int list_find(list_t* L, list_compare_t compare, const void* value)
{
    if (L != NULL)
    {
        for (size_t i = 0; i < L->size; ++i)
            if (compare(L->entries[i], value))
                return i;
    }
    return -1;
}

bool list_append(list_t* L, void* entry)
{
    if (L == NULL)
        return false;
    if (L->size >= L->capacity)
    {
        if (L->capacity == 0)
        {
            L->entries = malloc(4 * sizeof(void*));
            if (L->entries == NULL)
                return false;
            L->capacity = 4;
        }
        else
        {
            void** new_entries =
                realloc(L->entries, 2 * L->capacity * sizeof(void*));
            if (new_entries == NULL)
                return false;
            L->entries = new_entries;
            L->capacity *= 2;
        }
    }
    L->entries[L->size++] = entry;
    return true;
}

bool list_replace_at(list_t* L, size_t i, void* entry, list_deleter_t deleter)
{
    if (L == NULL)
        return false;
    if (i >= L->size)
        return false;
    if (deleter != NULL)
        deleter(L->entries[i]);
    L->entries[i] = entry;
    return true;
}

bool list_remove_at(list_t* L, size_t i, list_deleter_t deleter)
{
    if (L == NULL)
        return false;
    if (i >= L->size)
        return false;
    if (deleter != NULL)
        deleter(L->entries[i]);
    for (size_t j = i; j < L->size - 1; ++j)
        L->entries[j] = L->entries[j + 1];
    --L->size;
    return true;
}

size_t list_remove_if(list_t* L, list_predicate_t predicate,
                      list_deleter_t deleter)
{
    if (L == NULL)
        return 0;
    if (predicate == NULL)
        return 0;
    size_t j = 0, removed = 0;
    for (size_t i = 0; i < L->size; ++i)
    {
        if (predicate(L->entries[i]))
        {
            if (deleter != NULL)
                deleter(L->entries[i]);
            ++removed;
        }
        else
        {
            L->entries[j++] = L->entries[i];
        }
    }
    L->size = j;
    return removed;
}

size_t list_remove_if_value(list_t* L, list_compare_t compare,
                            const void* value, list_deleter_t deleter)
{
    if (L == NULL)
        return 0;
    if (compare == NULL)
        return 0;
    size_t j = 0, removed = 0;
    for (size_t i = 0; i < L->size; ++i)
    {
        if (compare(L->entries[i], value))
        {
            if (deleter != NULL)
                deleter(L->entries[i]);
            ++removed;
        }
        else
        {
            L->entries[j++] = L->entries[i];
        }
    }
    L->size = j;
    return removed;
}

size_t list_size(list_t* L)
{
    if (L != NULL)
        return L->size;
    return 0;
}

void list_clear(list_t* L, list_deleter_t deleter)
{
    if (L == NULL)
        return;
    if (deleter != NULL)
    {
        for (size_t i = 0; i < L->size; ++i)
        {
            deleter(L->entries[i]);
        }
    }
    L->size = 0;
}

void list_destroy(list_t* L, list_deleter_t deleter)
{
    if (L == NULL)
        return;
    list_clear(L, deleter);
    free(L->entries);
    free(L);
}

#define MAX_WD 16

struct file_watch_entry
{
    int wd;
    unsigned mask;
    char* path;
    char* filter;
    file_watch_cb_t cb;
};

struct file_watch
{
    int fd;
    list_t* entries;
};

#ifdef HAVE_SYS_INOTIFY_H
static bool file_watch__is_matching_wd(const void* value1, const void* value2)
{
    return ((const struct file_watch_entry*)value1)->wd == *(const int*)value2;
}

static bool file_watch__same_path(const void* value1, const void* value2)
{
    return strcmp(((const struct file_watch_entry*)value1)->path,
                  (const char*)value2)
           == 0;
}
#endif

static void file_watch__delete_entry(void* value)
{
    struct file_watch_entry* entry = (struct file_watch_entry*)value;
    free(entry->path);
    free(entry->filter);
    free(entry);
}

file_watch_t* file_watch_create()
{
#ifdef HAVE_SYS_INOTIFY_H
    file_watch_t* W = malloc(sizeof(file_watch_t));
    if (W != NULL)
    {
        W->fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        W->entries = list_create();
    }
    return W;
#else
    return NULL;
#endif
}

int file_watch_poll_fd(file_watch_t* W)
{
    return W != NULL ? W->fd : -1;
}

size_t file_watch_prepare_poll(file_watch_t* W, struct pollfd* pollfds,
                               size_t max_fds)
{
    if (W != NULL && max_fds >= 1)
    {
        pollfds[0].fd = W->fd;
        pollfds[0].events = POLLIN;
        pollfds[0].revents = 0;
        return 1;
    }
    return 0;
}

void file_watch_process_events(file_watch_t* W)
{
    if (W == NULL)
        return;
#ifdef HAVE_SYS_INOTIFY_H
    char buf[4096] ATTRIBUTE(aligned(__alignof__(struct inotify_event)));
    char filepath[1024];
    filepath[sizeof(filepath) - 1] = 0;
    const struct inotify_event* event;
    ssize_t size = read(W->fd, buf, sizeof(buf));
    if (size <= 0)
        return;
    size_t num_wd = list_size(W->entries);
    for (char* ptr = buf; ptr < buf + size;
         ptr += sizeof(struct inotify_event) + event->len)
    {
        event = (const struct inotify_event*)ptr;
        unsigned what = 0;
        if (event->mask & (IN_CREATE | IN_MOVED_TO))
            what |= FW_CREATED;
        if (event->mask & (IN_CLOSE_WRITE | IN_MOVED_TO))
            what |= FW_MODIFIED;
        if (event->mask & (IN_DELETE | IN_DELETE_SELF | IN_MOVED_FROM))
            what |= FW_DELETED;
        if (event->mask & IN_MODIFY)
            what |= FW_CHANGING;
        if (event->len > 0)
            log_debug("inotify: watch event [wd=%d, mask=%#x, name=\"%s\"]",
                      event->wd, event->mask, event->name);
        else
            log_debug("inotify: watch event [wd=%d, mask=%#x, name=NULL]",
                      event->wd, event->mask);
        if (what != 0)
        {
            for (size_t i = 0; i < num_wd; ++i)
            {
                const struct file_watch_entry* entry =
                    (const struct file_watch_entry*)list_get(W->entries, i);
                if (entry->wd == event->wd)
                {
                    if (entry->filter != NULL
                        && (event->len == 0
                            || strcmp(entry->filter, event->name) != 0))
                        continue;
                    char* fp_end =
                        stpncpy(filepath, entry->path, sizeof(filepath) - 1);
                    if (event->len > 0
                        && event->len
                               < sizeof(filepath) - (fp_end - filepath) - 2)
                    {
                        fp_end = stpcpy(fp_end, "/");
                        stpncpy(fp_end, event->name, event->len);
                        if (entry->mask
                            && list_find(W->entries, file_watch__same_path,
                                         filepath)
                                   < 0)
                        {
                            struct file_watch_entry* new_entry =
                                (struct file_watch_entry*)malloc(
                                    sizeof(struct file_watch_entry));
                            if (new_entry != NULL)
                            {
                                new_entry->wd = inotify_add_watch(
                                    W->fd, filepath, entry->mask);
                                new_entry->path = strdup(filepath);
                                new_entry->mask = 0;
                                new_entry->filter = NULL;
                                new_entry->cb = entry->cb;
                                list_append(W->entries, new_entry);
                                log_debug("inotify: watching '%s' [wd=%d]",
                                          new_entry->path, new_entry->wd);
                            }
                        }
                        log_debug("inotify: directory event for '%s'",
                                  event->name);
                    }
                    entry->cb(filepath, what, event->cookie);
                    break;
                }
            }
        }
        if (event->mask & IN_IGNORED)
        {
            log_debug("inotify: removing entries for [wd=%d]", event->wd);
            num_wd -=
                list_remove_if_value(W->entries, file_watch__is_matching_wd,
                                     &event->wd, file_watch__delete_entry);
        }
    }
#endif
}

bool file_watch_if_modified(file_watch_t* W, const char* path,
                            file_watch_cb_t callback)
{
    if (W == NULL || path == NULL || callback == NULL)
        return false;
#ifdef HAVE_SYS_INOTIFY_H
    struct file_watch_entry* entry =
        (struct file_watch_entry*)malloc(sizeof(struct file_watch_entry));
    if (entry == NULL)
        return false;
    entry->wd = inotify_add_watch(W->fd, path,
                                  IN_CLOSE_WRITE | IN_DELETE_SELF | IN_MODIFY);
    entry->mask = 0;
    entry->path = strdup(path);
    entry->filter = NULL;
    entry->cb = callback;
    list_append(W->entries, entry);
    log_debug("inotify: watching '%s' [wd=%d]", entry->path, entry->wd);
    const char* slash = strrchr(path, '/');
    if (slash != NULL)
    {
        entry =
            (struct file_watch_entry*)malloc(sizeof(struct file_watch_entry));
        entry->path = strndup(path, slash - path);
        entry->wd = inotify_add_watch(W->fd, entry->path, IN_MOVE | IN_CREATE);
        entry->mask = IN_CLOSE_WRITE | IN_DELETE_SELF | IN_MODIFY;
        entry->filter = strdup(slash + 1);
        entry->cb = callback;
        list_append(W->entries, entry);
        log_debug("inotify: watching '%s' [wd=%d]", entry->path, entry->wd);
    }
    return true;
#else
    return false;
#endif
}

bool file_watch_remove(file_watch_t* W, int wd)
{
    if (W != NULL && wd >= 0)
    {
#ifdef HAVE_SYS_INOTIFY_H
        return inotify_rm_watch(W->fd, wd) == 0;
#endif
    }
    return false;
}

void file_watch_destroy(file_watch_t* W)
{
    if (W != NULL)
    {
        list_destroy(W->entries, file_watch__delete_entry);
        close(W->fd);
        free(W);
    }
}

char* endpoint_for_redis(const char* s, int* port)
{
    if (s == NULL)
        return NULL;
    const char* colon = strchr(s, ':');
    const char* slash = strchr(s, '/');
    if (slash || !colon)
    {
        /* Treat this as unix socket path */
        *port = -1;
        return strdup(s);
    }
    if (colon == s)
        return NULL;
    char* end;
    *port = strtol(colon + 1, &end, 10);
    if (NONEMPTY_STRING(end) || *port <= 0)
        return NULL;
    return strndup(s, colon - s);
}

static enum log_priority log_prio = LogInfo;
static const char* priority_labels[] = {"debug: ", "", "warn: ", "error: "};

#ifdef HAVE_SYSLOG_H
static bool use_syslog = false;
static int syslog_priorities[] = {LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERR};
#endif

static void vlog(enum log_priority prio, const char* fmt, va_list ap)
{
    if (prio < log_prio)
        return;
    char buffer[1088];
    size_t prefix_len =
        snprintf(buffer, sizeof(buffer), "postsrsd: %s", priority_labels[prio]);
    char* text = buffer + prefix_len;
    vsnprintf(text, sizeof(buffer) - prefix_len, fmt,  // flawfinder: ignore
              ap);
    buffer[sizeof(buffer) - 1] = 0;
    fprintf(stderr, "%s\n", buffer);
    fflush(stderr);
#ifdef HAVE_SYSLOG_H
    if (use_syslog)
    {
        syslog(LOG_MAIL | syslog_priorities[prio], "%s", text);
    }
#endif
}

void log_enable_syslog()
{
#ifdef HAVE_SYSLOG_H
    if (!use_syslog)
    {
        time_t now;
        openlog("postsrsd", LOG_PID | LOG_NDELAY, LOG_MAIL);
        now = time(NULL);
        localtime(&now);
        use_syslog = true;
    }
#else
    log_warn("syslog facility is not available");
#endif
}

void log_disable_syslog()
{
#ifdef HAVE_SYSLOG_H
    if (use_syslog)
    {
        closelog();
        use_syslog = false;
    }
#endif
}

void log_set_verbosity(enum log_priority prio)
{
    log_prio = prio;
}

void log_debug(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog(LogDebug, fmt, ap);
    va_end(ap);
}

void log_info(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog(LogInfo, fmt, ap);
    va_end(ap);
}

void log_warn(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog(LogWarn, fmt, ap);
    va_end(ap);
}

void log_error(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog(LogError, fmt, ap);
    va_end(ap);
}

void log_perror(int err, const char* prefix)
{
    char* msg = strerror(err);
    if (NONEMPTY_STRING(prefix))
        log_error("%s: %s", prefix, msg);
    else
        log_error("%s", msg);
}

void log_fatal(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog(LogError, fmt, ap);
    va_end(ap);
    exit(EXIT_FAILURE);
}

bool sd_notify(const char* fmt, ...)
{
    char message[128];
    if (fmt == NULL)
        return false;
    va_list ap;
    va_start(ap, fmt);
    ssize_t message_len =
        vsnprintf(message, sizeof(message), fmt, ap);  // flawfinder: ignore
    if (message_len <= 0)
        return false;
#if defined(AF_UNIX)
    const char* notify_socket = getenv("NOTIFY_SOCKET");
    if (notify_socket == NULL)
        return false;
    struct sockaddr_un sa;
    size_t sock_len = strlen(notify_socket);
    if (sock_len >= sizeof(sa.sun_path))
    {
        log_error("NOTIFY_SOCKET path is too big");
        return false;
    }
    sa.sun_family = AF_UNIX;
    memcpy(sa.sun_path, notify_socket, sock_len);
    if (sa.sun_path[0] == '@')
        sa.sun_path[0] = 0;
    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
    {
        log_perror(errno, "sd_notify socket");
        return false;
    }
    if (connect(fd, (const struct sockaddr*)&sa,
                offsetof(struct sockaddr_un, sun_path) + sock_len)
        < 0)
    {
        log_perror(errno, "sd_notify connect");
        close(fd);
        return false;
    }
    ssize_t written = write(fd, message, message_len);
    if (written != (ssize_t)message_len)
    {
        log_perror(EPROTO, "sd_notify write");
        close(fd);
        return false;
    }
    close(fd);
    return true;
#endif
}

sandbox_t* sandbox_init()
{
#ifdef WITH_SECCOMP
    scmp_filter_ctx scmp_ctx;
    scmp_ctx = seccomp_init(SCMP_ACT_KILL_PROCESS);
    if (scmp_ctx == NULL)
        return NULL;
    /* Syscalls without database access */
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(fstat), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(stat), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(read), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(readv), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(write), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(writev), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(alarm), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(setitimer), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(sigreturn), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(rt_sigreturn), 0)
        < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(close), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(exit), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(exit_group), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(time), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(brk), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(madvise), 0) < 0)
        goto fail;
#    ifdef HAVE_SYS_MMAN_H
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(mprotect), 1,
                         SCMP_A2(SCMP_CMP_MASKED_EQ, PROT_EXEC, 0))
        < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(mmap), 1,
                         SCMP_A2(SCMP_CMP_MASKED_EQ, PROT_EXEC, 0))
        < 0)
        goto fail;
#    endif
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(munmap), 0) < 0)
        goto fail;
#    ifdef WITH_SQLITE
    /* Syscalls for SQlite database access */
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(getpid), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(geteuid), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(lseek), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(newfstatat), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(fcntl), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(open), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(openat), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(pread64), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(preadv2), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(pwrite64), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(pwritev2), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(fsync), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(fdatasync), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(unlink), 0) < 0)
        goto fail;
#    endif
#    ifdef WITH_REDIS
    /* Syscalls for Redis database access */
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(sendto), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(recvfrom), 0) < 0)
        goto fail;
#    endif
#    ifdef __SANITIZE_ADDRESS__
    /* These syscalls are used by the Address Sanitizer */
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(sigaltstack), 0)
        < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(rt_sigprocmask), 0)
        < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(rt_sigaction), 0)
        < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(gettid), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(getppid), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(futex), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(prctl), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(clone), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(wait4), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(sched_yield), 0)
        < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(ptrace), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(getdents64), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW,
                         SCMP_SYS(readlink),  // flawfinder: ignore
                         0)
        < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ERRNO(EINVAL), SCMP_SYS(ioctl), 0)
        < 0)
        goto fail;
#    endif
    return (sandbox_t*)scmp_ctx;
fail:
    seccomp_release(scmp_ctx);
    return NULL;
#else
    return NULL;
#endif
}

bool sandbox_enable(sandbox_t* sandbox)
{
    MAYBE_UNUSED(sandbox);
#ifdef WITH_SECCOMP
    if (sandbox == NULL)
        return false;
    return seccomp_load((scmp_filter_ctx)sandbox) == 0;
#else
    return false;
#endif
}

void sandbox_release(sandbox_t* sandbox)
{
    MAYBE_UNUSED(sandbox);
#ifdef WITH_SECCOMP
    if (sandbox != NULL)
        seccomp_release((scmp_filter_ctx)sandbox);
#endif
}
