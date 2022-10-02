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
#include "srs.h"
#include <string.h>

char* postsrsd_forward(char* buffer, size_t bufsize, const char* addr,
                       const char* domain, srs_t* srs, database_t* db,
                       domain_set_t* local_domains)
{
    const char* at = strchr(addr, '@');
    if (!at)
    {
        log_info("<%s> not rewritten: no domain", addr);
        return NULL;
    }
    const char* input_domain = at + 1;
    if (domain_set_contains(local_domains, input_domain))
    {
        log_info("<%s> not rewritten: local domain");
        return NULL;
    }
    (void)buffer;
    (void)bufsize;
    (void)domain;
    (void)srs;
    (void)db;
    return NULL;
}

char* postsrsd_reverse(char* buffer, size_t bufsize, const char* addr,
                       const char* domain, srs_t* srs, database_t* db,
                       domain_set_t* local_domains)
{
    (void)buffer;
    (void)bufsize;
    (void)addr;
    (void)domain;
    (void)srs;
    (void)db;
    (void)local_domains;
    return NULL;
}
