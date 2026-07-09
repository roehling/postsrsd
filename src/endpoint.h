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
#ifndef ENDPOINT_H
#define ENDPOINT_H

#include <stdbool.h>
#include <stddef.h>

struct pollfd;
struct endpoint;
typedef struct endpoint endpoint_t;

endpoint_t* endpoint_create(const char* s);
void endpoint_destroy(endpoint_t* endpoint);
void endpoint_release(endpoint_t* endpoint);
size_t endpoint_prepare_poll(endpoint_t* endpoint, struct pollfd* pollfds,
                             size_t max_fds);

#endif
