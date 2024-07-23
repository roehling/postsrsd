/* PostSRSd - Sender Rewriting Scheme daemon for Postfix
 * Copyright 2012-2024 Timo Röhling <timo@gaussglocke.de>
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
#include "config.h"

#include <check.h>
#include <stdlib.h>
#include <unistd.h>

static char pwd[500];
static char tmpdir[sizeof(pwd) + 7];

void setup_fs()
{
    ck_assert_ptr_nonnull(getcwd(pwd, sizeof(pwd)));
    strcpy(tmpdir, pwd);
    strcat(tmpdir, "/XXXXXX");
    ck_assert_ptr_eq(mkdtemp(tmpdir), tmpdir);
    ck_assert_int_eq(chdir(tmpdir), 0);
}

void teardown_fs()
{
    ck_assert_int_eq(chdir(pwd), 0);
    ck_assert_int_eq(rmdir(tmpdir), 0);
}

START_TEST(config_domains_file)
{
    FILE* f = fopen("domains.txt", "w");
    fprintf(f,
            "# This is a comment at the beginning of the file\n"
            "example.com\n"
            "     # This is a comment with preceding white space\n"
            "\t tabspace.org\n"
            "\n"
            "commented.de   # This is a comment after a domain name\n"
            "trailing.net    ");
    fclose(f);
    cfg_t* cfg = config_defaults();
    cfg_setstr(cfg, "domains-file", "domains.txt");

    domain_set_t* D = NULL;
    char* srs_domain = NULL;
    ck_assert_int_eq(srs_domains_from_config(cfg, &srs_domain, &D), true);
    ck_assert_int_eq(domain_set_contains(D, "commented.de"), true);
    ck_assert_int_eq(domain_set_contains(D, "example.com"), true);
    ck_assert_int_eq(domain_set_contains(D, "tabspace.org"), true);
    ck_assert_int_eq(domain_set_contains(D, "trailing.net"), true);
    ck_assert_str_eq(srs_domain, "example.com");
    ck_assert_int_eq(unlink("domains.txt"), 0);
    domain_set_destroy(D);
    free(srs_domain);
    cfg_free(cfg);
}
END_TEST

BEGIN_TEST_SUITE(config)
ADD_TEST_CASE_WITH_UNCHECKED_FIXTURE(fs, setup_fs, teardown_fs)
ADD_TEST_TO_TEST_CASE(fs, config_domains_file)
END_TEST_SUITE()
TEST_MAIN(config)
