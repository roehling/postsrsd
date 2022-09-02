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

#define TEST_MAIN(suite) \
    int main()                                                      \
    {                                                               \
        Suite* s = suite ## _suite();                               \
        SRunner* sr = srunner_create(s);                            \
                                                                    \
        srunner_run_all(sr, CK_NORMAL);                             \
        int number_failed = srunner_ntests_failed(sr);              \
        srunner_free(sr);                                           \
        return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;  \
    }

#endif
