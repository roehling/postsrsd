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
#ifndef MAIN_H
#define MAIN_H

#include "config.h"
#include "endpoint.h"
#include "srs2.h"
#include "util.h"

struct postsrsd
{
    cfg_t* cfg;
    srs_t* srs;
    endpoint_t* socketmap;
    endpoint_t* milter;
    char* srs_domain;
    domain_set_t* local_domains;
    file_watch_t* file_watch;
    int target_uid, target_gid;
    size_t connection_limit;
};
typedef struct postsrsd postsrsd_t;

void init_state(postsrsd_t* state);
void finalize_state(postsrsd_t* state);

void handle_socketmap_client(postsrsd_t* state, int conn);
void handle_milter_client(postsrsd_t* state, int conn);

#endif
