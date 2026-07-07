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
#ifndef DATABASE_H
#define DATABASE_H

#include <stdbool.h>

struct database;
typedef struct database database_t;

database_t* database_connect(const char* uri, bool create_if_not_exist);
char* database_read(database_t* db, const char* key);
bool database_write(database_t* db, const char* key, const char* value,
                    unsigned lifetime);
void database_expire(database_t* db);
void database_disconnect(database_t* db);

#endif
