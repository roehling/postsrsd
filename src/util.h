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
#ifndef UTIL_H
#define UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>

#define MAYBE_UNUSED(x) (void)(x)

#ifdef __GNUC__
#    define ATTRIBUTE(x) __attribute__((x))
#else
#    define ATTRIBUTE(x)
#endif

#define NONEMPTY_STRING(s)      ((s) != NULL && *(s) != 0)
#define NULL_OR_EMPTY_STRING(s) ((s) == NULL || *(s) == 0)

struct pollfd;
struct domain_set;
typedef struct domain_set domain_set_t;
struct pid_set;
typedef struct pid_set pid_set_t;
struct list;
typedef struct list list_t;
typedef void (*list_deleter_t)(void*);
typedef bool (*list_predicate_t)(const void*);
typedef bool (*list_compare_t)(const void*, const void*);
struct file_watch;
typedef struct file_watch file_watch_t;
typedef void (*file_watch_cb_t)(const char*, unsigned, size_t);

#define FW_CREATED  1
#define FW_MODIFIED 2
#define FW_DELETED  4
#define FW_CHANGING 8

bool string_equal(const void* s1, const void* s2);
void string_set(char** var, char* value);
char* b32h_encode(const char* data, size_t length, char* buffer,
                  size_t bufsize);

bool file_exists(const char* filename);
bool directory_exists(const char* dirname);

int lock_acquire(const char* path);
void lock_release(const char* path, int fd);

bool read_all(int fd, void* buffer, size_t size);
bool writev_all(int fd, struct iovec* iov, size_t numv);

domain_set_t* domain_set_create();
bool domain_set_add(domain_set_t* D, const char* domain);
bool domain_set_contains(domain_set_t* D, const char* domain);
void domain_set_destroy(domain_set_t* D);

pid_set_t* pid_set_create();
bool pid_set_add(pid_set_t* P, pid_t pid);
bool pid_set_remove(pid_set_t* P, pid_t pid);
bool pid_set_kill(pid_set_t* P, int signal);
void pid_set_wait(pid_set_t* P);
void pid_set_destroy(pid_set_t* P);

list_t* list_create();
void* list_get(list_t* L, size_t i);
int list_find(list_t* L, list_compare_t compare, const void* value);
bool list_append(list_t* L, void* entry);
size_t list_size(list_t* L);
bool list_replace_at(list_t* L, size_t i, void* entry, list_deleter_t deleter);
bool list_remove_at(list_t* L, size_t i, list_deleter_t deleter);
size_t list_remove_if(list_t* L, list_predicate_t predicate,
                      list_deleter_t deleter);
size_t list_remove_if_value(list_t* L, list_compare_t compare,
                            const void* value, list_deleter_t deleter);
void list_clear(list_t* L, list_deleter_t deleter);
void list_destroy(list_t* L, list_deleter_t deleter);

file_watch_t* file_watch_create();
int file_watch_poll_fd(file_watch_t* W);
size_t file_watch_prepare_poll(file_watch_t* W, struct pollfd* pollfds,
                               size_t max_fds);
void file_watch_process_events(file_watch_t* W);
bool file_watch_if_modified(file_watch_t* W, const char* path,
                            file_watch_cb_t callback);
void file_watch_destroy(file_watch_t* W);

char* endpoint_for_redis(const char* s, int* port);

enum log_priority
{
    LogDebug,
    LogInfo,
    LogWarn,
    LogError,
};

void log_enable_syslog();
void log_disable_syslog();
void log_set_verbosity(enum log_priority prio);
void log_debug(const char* fmt, ...)
    ATTRIBUTE(format(printf, 1, 2));  // flawfinder: ignore
void log_info(const char* fmt, ...)
    ATTRIBUTE(format(printf, 1, 2));  // flawfinder: ignore
void log_warn(const char* fmt, ...)
    ATTRIBUTE(format(printf, 1, 2));  // flawfinder: ignore
void log_error(const char* fmt, ...)
    ATTRIBUTE(format(printf, 1, 2));  // flawfinder: ignore
void log_perror(int errno, const char* prefix);
void log_fatal(const char* fmt, ...) ATTRIBUTE(noreturn);

bool sd_notify(const char* fmt, ...)
    ATTRIBUTE(format(printf, 1, 2));  // flawfinder: ignore

struct sandbox;
typedef struct sandbox sandbox_t;

sandbox_t* sandbox_init();
bool sandbox_enable(sandbox_t* sandbox);
void sandbox_release(sandbox_t* sandbox);

typedef void (*signal_handler_t)(int);
int signal_set_handler(int signum, signal_handler_t handler);
int signal_set_handler_once(int signum, signal_handler_t handler);
int signal_ignore(int signum);
int signal_reset_handler(int signum);

#endif
