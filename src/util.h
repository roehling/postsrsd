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
#ifndef UTIL_H
#define UTIL_H

#include <stdbool.h>
#include <stddef.h>

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

bool string_equal(const void* s1, const void* s2);
void set_string(char** var, char* value);
char* b32h_encode(const char* data, size_t length, char* buffer,
                  size_t bufsize);

char** argvdup(char** argv);
void freeargv(char** argv);

char* strip_brackets(const char* addr);
char* add_brackets(const char* addr);

bool file_exists(const char* filename);
bool directory_exists(const char* dirname);

int acquire_lock(const char* path);
void release_lock(const char* path, int fd);

domain_set_t* domain_set_create();
bool domain_set_add(domain_set_t* D, const char* domain);
bool domain_set_contains(domain_set_t* D, const char* domain);
void domain_set_destroy(domain_set_t* D);

list_t* list_create();
void* list_get(list_t* L, size_t i);
int list_find(list_t* L, list_compare_t compare, const void* value);
bool list_append(list_t* L, void* entry);
size_t list_size(list_t* L);
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

char* endpoint_for_milter(const char* s);
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
void log_debug(const char* fmt, ...) ATTRIBUTE(format(printf, 1, 2));
void log_info(const char* fmt, ...) ATTRIBUTE(format(printf, 1, 2));
void log_warn(const char* fmt, ...) ATTRIBUTE(format(printf, 1, 2));
void log_error(const char* fmt, ...) ATTRIBUTE(format(printf, 1, 2));
void log_perror(int errno, const char* prefix);
void log_fatal(const char* fmt, ...) ATTRIBUTE(noreturn);

#endif
