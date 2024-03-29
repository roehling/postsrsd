/* PostSRSd - Sender Rewriting Scheme daemon for Postfix
 * Copyright 2012-2023 Timo Röhling <timo@gaussglocke.de>
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
#ifdef HAVE_FCNTL_H
#    include <fcntl.h>
#endif
#ifdef HAVE_SYS_FILE_H
#    include <sys/file.h>
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
#ifdef HAVE_SYS_TIME_H
#    include <sys/time.h>
#endif
#ifdef HAVE_TIME_H
#    include <time.h>
#endif
#include <unistd.h>

#ifndef HAVE_STRNCASECMP
#    ifdef HAVE__STRNICMP
#        define strncasecmp _strnicmp
#    endif
#endif

void set_string(char** var, char* value)
{
    free(*var);
    *var = value;
}

char** argvdup(char** argv)
{
    if (argv == NULL)
        return NULL;
    size_t num = 0;
    while (argv[num] != NULL)
        num++;
    char** result = malloc((num + 1) * sizeof(char*));
    if (result == NULL)
        return NULL;
    for (size_t i = 0; i < num; ++i)
    {
        result[i] = strdup(argv[i]);
        if (result[i] == NULL)
        {
            for (size_t j = 0; j < i; ++j)
            {
                free(result[j]);
            }
            free(result);
            return NULL;
        }
    }
    result[num] = NULL;
    return result;
}

void freeargv(char** argv)
{
    if (argv == NULL)
        return;
    size_t i = 0;
    while (argv[i] != NULL)
    {
        free(argv[i]);
        i++;
    }
    free(argv);
}

char* strip_brackets(const char* addr)
{
    const char* lbrak = strchr(addr, '<');
    const char* rbrak = strchr(addr, '>');
    if (lbrak == NULL || rbrak == NULL)
        return NULL;
    lbrak++;
    char* bare = strdup(lbrak);
    *(bare + (rbrak - lbrak)) = 0;
    return bare;
}

char* add_brackets(const char* addr)
{
    char* result = malloc(strlen(addr) + 3);
    result[0] = '<';
    strcpy(result + 1, addr);
    strcat(result, ">");
    return result;
}

char* b32h_encode(const char* data, size_t length, char* buffer, size_t bufsize)
{
    static const char B32H_CHARS[32] = "0123456789ABCDEFGHIJKLMNOPQRSTUV";
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

int acquire_lock(const char* path)
{
    MAYBE_UNUSED(path);
#if defined(LOCK_EX) && defined(LOCK_NB)
    size_t len = strlen(path);
    char* lock_path = malloc(len + 6); /* ".lock" + "\0" */
    strcpy(lock_path, path);
    strcat(lock_path, ".lock");
    int fd = open(lock_path, O_RDONLY | O_CREAT, 0600);
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

void release_lock(const char* path, int fd)
{
    MAYBE_UNUSED(path);
    MAYBE_UNUSED(fd);
#if defined(LOCK_EX) && defined(LOCK_NB)
    size_t len = strlen(path);
    char* lock_path = malloc(len + 6); /* ".lock" + "\0" */
    strcpy(lock_path, path);
    strcat(lock_path, ".lock");
    unlink(lock_path);
    free(lock_path);
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
    if (D->s)
        domain_set_destroy(D->s);
    free(D);
}

static bool walk_domain_set(domain_set_t* D, char* domain, int flags)
{
    char* dot = strrchr(domain, '.');
    char* subdomain = domain;
    if (dot)
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
    if (dot)
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
    char buffer[1024];
    strncpy(buffer, domain, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = 0;
    return !walk_domain_set(D, buffer, DOMAIN_SET_ADD);
}

bool domain_set_contains(domain_set_t* D, const char* domain)
{
    if (D == NULL)
        return false;
    char buffer[1024];
    strncpy(buffer, domain, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = 0;
    return walk_domain_set(D, buffer, DOMAIN_SET_PARENTS_MATCH);
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
    if (deleter)
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

static char* swap_host_port(const char* s, size_t prefix_len)
{
    char* port = strchr(s + prefix_len, ':');
    if (port == NULL)
        return NULL;
    if (*(port + 1) == 0)
        return NULL;
    ptrdiff_t host_len = (port - s) - prefix_len;
    if (host_len == 0)
        return NULL;
    char* result = strdup(s);
    if (result == NULL)
        return NULL;
    strcpy(result + prefix_len, port + 1);
    if (host_len > 1 || s[prefix_len] != '*')
    {
        strcat(result, "@");
        strncat(result, s + prefix_len, host_len);
    }
    return result;
}

char* endpoint_for_milter(const char* s)
{
    if (s == NULL)
        return NULL;
    if (strncasecmp(s, "unix:", 5) == 0 || strncasecmp(s, "local:", 6) == 0)
        return strdup(s);
    if (strncasecmp(s, "inet:", 5) == 0)
    {
        return swap_host_port(s, 5);
    }
    if (strncasecmp(s, "inet6:", 6) == 0)
    {
        return swap_host_port(s, 6);
    }
    return NULL;
}

char* endpoint_for_redis(const char* s, int* port)
{
    if (s == NULL)
        return NULL;
    char* colon = strchr(s, ':');
    char* slash = strchr(s, '/');
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
    vsnprintf(text, sizeof(buffer) - prefix_len, fmt, ap);
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
    time_t now;
    openlog("postsrsd", LOG_PID | LOG_NDELAY, LOG_MAIL);
    now = time(NULL);
    localtime(&now);
    use_syslog = true;
#else
    log_warn("syslog facility is not available");
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
    if (prefix)
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
    exit(1);
}
