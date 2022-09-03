/* PostSRSd - Sender Rewriting Scheme daemon for Postfix
 * Copyright 2012-2022 Timo Röhling <timo@gaussglocke.de>
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

#include <stddef.h>

struct domain_set;

void set_string(char** var, char* value);
char* b32h_encode(const char* data, size_t length, char* buffer,
                  size_t bufsize);

int file_exists(const char* filename);
int directory_exists(const char* dirname);

int acquire_lock(const char* path);
void release_lock(const char* path, int fd);

struct domain_set* domain_set_create();
int domain_set_add(struct domain_set* D, const char* domain);
int domain_set_contains(struct domain_set* D, const char* domain);
void domain_set_destroy(struct domain_set* D);

#endif
