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
#include "common.h"
#include "database.h"

#include <check.h>
#include <postsrsd_build_config.h>
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#    include <unistd.h>
#endif

START_TEST(invalid_database)
{
    ck_assert_ptr_null(database_connect("invalid:", true));
}
END_TEST

#ifdef WITH_SQLITE
START_TEST(database_sqlite_key_value)
{
    database_t* db = database_connect("sqlite::memory:", true);
    ck_assert_ptr_nonnull(db);
    ck_assert_ptr_null(database_read(db, "mykey"));
    database_write(db, "mykey", "myvalue", 1);
    char* value = database_read(db, "mykey");
    ck_assert_str_eq(value, "myvalue");
    free(value);
    database_disconnect(db);
}
END_TEST

START_TEST(database_sqlite_expiry)
{
    database_t* db = database_connect("sqlite::memory:", true);
    ck_assert_ptr_nonnull(db);
    database_write(db, "mykey", "myvalue", 0);
    char* value = database_read(db, "mykey");
    ck_assert_str_eq(value, "myvalue");
    free(value);
    database_expire(db);
    ck_assert_ptr_null(database_read(db, "mykey"));
    database_disconnect(db);
}
END_TEST
#endif

#ifdef WITH_REDIS
START_TEST(database_redis_key_value)
{
    database_t* db = database_connect("redis:localhost:6379", true);
    if (db == NULL)
        return; /* skip test if no redis server is available */
    database_write(db, "mykey", "myvalue", 1);
    char* value = database_read(db, "mykey");
    ck_assert_str_eq(value, "myvalue");
    free(value);
    database_disconnect(db);
}
END_TEST

START_TEST(database_redis_expiry)
{
    database_t* db = database_connect("redis:localhost:6379", true);
    if (db == NULL)
        return; /* skip test if no redis server is available */
    database_write(db, "mykey", "myvalue", 1);
    char* value = database_read(db, "mykey");
    ck_assert_str_eq(value, "myvalue");
    free(value);
    sleep(2);
    database_expire(db);
    ck_assert_ptr_null(database_read(db, "mykey"));
    database_disconnect(db);
}
END_TEST
#endif

BEGIN_TEST_SUITE(database)
ADD_TEST(invalid_database)
#ifdef WITH_SQLITE
ADD_TEST(database_sqlite_key_value)
ADD_TEST(database_sqlite_expiry)
#endif
#ifdef WITH_REDIS
ADD_TEST(database_redis_key_value)
ADD_TEST(database_redis_expiry)
#endif
END_TEST_SUITE()
TEST_MAIN(database)
