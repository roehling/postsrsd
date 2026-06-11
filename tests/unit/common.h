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
#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <stdlib.h>

#define BEGIN_TEST_SUITE(suite)               \
    static Suite* suite##_suite()             \
    {                                         \
        Suite* ts = suite_create(#suite);     \
        TCase* tcase = tcase_create("tcase"); \
        suite_add_tcase(ts, tcase);

#define ADD_TEST_CASE(tcase)             \
    TCase* tcase = tcase_create(#tcase); \
    suite_add_tcase(ts, tcase);

#define ADD_TEST_CASE_WITH_UNCHECKED_FIXTURE(tcase, setup, teardown) \
    ADD_TEST_CASE(tcase)                                             \
    tcase_add_checked_fixture(tcase, setup, teardown);

#define ADD_TEST_TO_TEST_CASE(tcase, testfunc) tcase_add_test(tcase, testfunc);

#define ADD_TEST(testfunc) ADD_TEST_TO_TEST_CASE(tcase, testfunc)

#define END_TEST_SUITE() \
    return ts;           \
    }

#define TEST_MAIN(suite)                                           \
    int main()                                                     \
    {                                                              \
        Suite* s = suite##_suite();                                \
        SRunner* sr = srunner_create(s);                           \
                                                                   \
        srunner_run_all(sr, CK_VERBOSE);                           \
        int number_failed = srunner_ntests_failed(sr);             \
        srunner_free(sr);                                          \
        return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE; \
    }

#endif
