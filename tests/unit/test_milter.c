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
#include "common.h"
#include "milter.h"

#include <check.h>

START_TEST(milter_macros)
{
    static const char macro_block[] =
        "a\000alpha\000\000unused\000b\000\000variable\000value";
    char* result;

    result = milter_find_macro("a", macro_block, sizeof(macro_block));
    ck_assert_ptr_nonnull(result);
    ck_assert_str_eq(result, "alpha");
    free(result);

    result = milter_find_macro("b", macro_block, sizeof(macro_block));
    ck_assert_ptr_nonnull(result);
    ck_assert_str_eq(result, "");
    free(result);

    result = milter_find_macro("variable", macro_block, sizeof(macro_block));
    ck_assert_ptr_nonnull(result);
    ck_assert_str_eq(result, "value");
    free(result);

    result = milter_find_macro("missing", macro_block, sizeof(macro_block));
    ck_assert_ptr_null(result);

    static const char broken_1[] = {'a'};

    result = milter_find_macro("a", broken_1, sizeof(broken_1));
    ck_assert_ptr_null(result);
    result = milter_find_macro("aaa", broken_1, sizeof(broken_1));
    ck_assert_ptr_null(result);

    static const char broken_2[] = {'a', 0, '1', 0, 'b', 0};

    result = milter_find_macro("a", broken_2, sizeof(broken_2));
    ck_assert_ptr_nonnull(result);
    ck_assert_str_eq(result, "1");
    free(result);

    result = milter_find_macro("b", broken_2, sizeof(broken_2));
    ck_assert_ptr_null(result);
}
END_TEST

START_TEST(milter_str_list)
{
    static const char list_data[] = "one\000two\000three\000\000five";

    list_t* L = list_create();
    milter_parse_str_list(L, list_data, sizeof(list_data));
    ck_assert_uint_eq(list_size(L), 5);
    ck_assert_str_eq(list_get(L, 0), "one");
    ck_assert_str_eq(list_get(L, 1), "two");
    ck_assert_str_eq(list_get(L, 2), "three");
    ck_assert_str_eq(list_get(L, 3), "");
    ck_assert_str_eq(list_get(L, 4), "five");
    list_clear(L, free);

    static const char missing_nul[] = {'o', 'n', 'e'};

    milter_parse_str_list(L, missing_nul, sizeof(missing_nul));
    ck_assert_uint_eq(list_size(L), 1);
    ck_assert_str_eq(list_get(L, 0), "one");
    list_destroy(L, free);
}
END_TEST

START_TEST(milter_test_parse_address)
{
    char* result;
    result = milter_parse_address("<test@example.com");
    ck_assert_ptr_null(result);
    result = milter_parse_address("test@example.com>");
    ck_assert_ptr_null(result);
    result = milter_parse_address(">test@example.com<");
    ck_assert_ptr_null(result);
    result = milter_parse_address("<test@example.com>");
    ck_assert_str_eq(result, "test@example.com");
    free(result);
    result = milter_parse_address("test@example.com");
    ck_assert_str_eq(result, "test@example.com");
    free(result);
    result = milter_parse_address("Test User <test@example.com>");
    ck_assert_str_eq(result, "test@example.com");
    free(result);
    result = milter_parse_address("<>");
    ck_assert_str_eq(result, "");
    free(result);
    result = milter_parse_address("<first><second>");
    ck_assert_str_eq(result, "first");
    free(result);
    ck_assert_ptr_null(milter_parse_address(NULL));
}
END_TEST

START_TEST(milter_test_parse_address_n)
{
    char* result;
    result = milter_parse_address_n("<test@example.com", 17);
    ck_assert_ptr_null(result);
    result = milter_parse_address_n("<test@example.com>", 17);
    ck_assert_ptr_null(result);
    result = milter_parse_address_n("test@example.com>", 17);
    ck_assert_ptr_null(result);
    result = milter_parse_address_n(">test@example.com<", 18);
    ck_assert_ptr_null(result);
    result = milter_parse_address_n("<test@example.com>", 18);
    ck_assert_str_eq(result, "test@example.com");
    free(result);
    result = milter_parse_address_n("test@example.com", 16);
    ck_assert_str_eq(result, "test@example.com");
    free(result);
    result = milter_parse_address_n("Test User <test@example.com>", 28);
    ck_assert_str_eq(result, "test@example.com");
    free(result);
    result = milter_parse_address_n("Test User <test@example.com>", 11);
    ck_assert_ptr_null(result);
    result = milter_parse_address_n("<>", 2);
    ck_assert_str_eq(result, "");
    free(result);
    result = milter_parse_address_n("<first><second>", 15);
    ck_assert_str_eq(result, "first");
    free(result);
    ck_assert_ptr_null(milter_parse_address_n(NULL, 10000));
}
END_TEST

BEGIN_TEST_SUITE(milter)
ADD_TEST(milter_macros)
ADD_TEST(milter_str_list)
ADD_TEST(milter_test_parse_address)
ADD_TEST(milter_test_parse_address_n)
END_TEST_SUITE()
TEST_MAIN(milter)
