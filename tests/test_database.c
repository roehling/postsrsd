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
#include <stdlib.h>

START_TEST(invalid_database)
{
    ck_assert_ptr_null(database_connect("invalid:", true));
}
END_TEST

START_TEST(database_key_value)
{
    struct db_conn* conn = database_connect("sqlite::memory:", true);
    ck_assert_ptr_nonnull(conn);
    ck_assert_ptr_null(database_read(conn, "mykey"));
    database_write(conn, "mykey", "myvalue", 1);
    char* value = database_read(conn, "mykey");
    ck_assert_str_eq(value, "myvalue");
    free(value);
    database_disconnect(conn);
}
END_TEST

START_TEST(database_expiry)
{
    struct db_conn* conn = database_connect("sqlite::memory:", true);
    ck_assert_ptr_nonnull(conn);
    database_write(conn, "mykey", "myvalue", 0);
    char* value = database_read(conn, "mykey");
    ck_assert_str_eq(value, "myvalue");
    free(value);
    database_expire(conn);
    ck_assert_ptr_null(database_read(conn, "mykey"));
    database_disconnect(conn);
}
END_TEST

BEGIN_TEST_SUITE(database)
ADD_TEST(invalid_database)
ADD_TEST(database_key_value)
ADD_TEST(database_expiry)
END_TEST_SUITE()
TEST_MAIN(database)
