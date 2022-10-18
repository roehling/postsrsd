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
#ifndef SRS_H
#define SRS_H

#include "database.h"
#include "srs2.h"
#include "util.h"

#include <stdbool.h>

char* postsrsd_forward(const char* addr, const char* domain, srs_t* srs,
                       database_t* db, domain_set_t* local_domains,
                       bool* error);
char* postsrsd_reverse(const char* addr, srs_t* srs, database_t* db,
                       bool* error);

#endif
