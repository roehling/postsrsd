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
#include "common.h"
#include "srs2.h"

#include <check.h>

srs_t* create_srs_t()
{
    srs_t* srs = srs_new();
    srs->faketime = 1577836860; /* 2020-01-01 00:01:00 UTC */
    srs_add_secret(srs, "tops3cr3t");
    return srs;
}

START_TEST(srs2_forwarding)
{
    srs_t* srs = create_srs_t();
    char* output = NULL;
    int result;

    result = srs_forward_alloc(srs, &output, "test@example.com", "example.com");
    ck_assert_int_eq(result, SRS_SUCCESS);
    ck_assert_str_eq(output, "test@example.com");
    free(output);

    result =
        srs_forward_alloc(srs, &output, "test@otherdomain.com", "example.com");
    ck_assert_int_eq(result, SRS_SUCCESS);
    ck_assert_str_eq(output, "SRS0=vmyz=2W=otherdomain.com=test@example.com");
    free(output);

    result = srs_forward_alloc(srs, &output, "foo", "example.com");
    ck_assert_int_eq(result, SRS_ENOSENDERATSIGN);

    srs_free(srs);
}
END_TEST

START_TEST(srs2_reversing)
{
    srs_t* srs = create_srs_t();
    char* output = NULL;
    int result;

    result = srs_reverse_alloc(srs, &output, "test@example.com");
    ck_assert_int_eq(result, SRS_ENOTSRSADDRESS);

    result = srs_reverse_alloc(srs, &output,
                               "SRS0=vmyz=2W=otherdomain.com=test@example.com");
    ck_assert_int_eq(result, SRS_SUCCESS);
    ck_assert_str_eq(output, "test@otherdomain.com");
    free(output);

    srs_free(srs);
}
END_TEST

BEGIN_TEST_SUITE(srs2)
ADD_TEST(srs2_forwarding);
ADD_TEST(srs2_reversing);
END_TEST_SUITE()
TEST_MAIN(srs2)
