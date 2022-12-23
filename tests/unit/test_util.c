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
#include "postsrsd_build_config.h"
#include "util.h"

#include <check.h>
#include <stdio.h>
#ifdef HAVE_SYS_FILE_H
#    include <sys/file.h>
#endif
#ifdef HAVE_SYS_STAT_H
#    include <sys/stat.h>
#endif
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

START_TEST(util_file_exists)
{
    ck_assert(!file_exists("testfile"));
    ck_assert(!file_exists("testdir"));

    ck_assert_int_eq(mkdir("testdir", 0755), 0);
    FILE* f = fopen("testfile", "w");
    fwrite("Test", 4, 1, f);
    fclose(f);

    ck_assert(file_exists("testfile"));
    ck_assert(!file_exists("testdir"));

    ck_assert_int_eq(unlink("testfile"), 0);
    ck_assert_int_eq(rmdir("testdir"), 0);

    ck_assert(!file_exists("testfile"));
}
END_TEST

START_TEST(util_directory_exists)
{
    ck_assert(!directory_exists("testfile"));
    ck_assert(!directory_exists("testdir"));

    ck_assert_int_eq(mkdir("testdir", 0755), 0);
    FILE* f = fopen("testfile", "w");
    fwrite("Test", 4, 1, f);
    fclose(f);

    ck_assert(directory_exists("testdir"));
    ck_assert(!directory_exists("testfile"));

    ck_assert_int_eq(unlink("testfile"), 0);
    ck_assert_int_eq(rmdir("testdir"), 0);

    ck_assert(!directory_exists("testdir"));
}
END_TEST

START_TEST(util_set_string)
{
    char* s = NULL;
    set_string(&s, strdup("Test"));
    ck_assert_str_eq(s, "Test");
    set_string(&s, NULL);
    ck_assert_ptr_null(s);
}
END_TEST

START_TEST(util_argvdup)
{
    const char* tokens[5] = {"one", "two", "three", "four", "five"};
    char* argv[5];
    ck_assert_ptr_null(argvdup(NULL));
    for (size_t num = 0; num < 5; ++num)
    {
        for (size_t i = 0; i < num; ++i)
        {
            argv[i] = strdup(tokens[i]);
        }
        argv[num] = NULL;
        char** result = argvdup(argv);
        ck_assert_ptr_ne(argv, result);
        for (size_t i = 0; i < num; ++i)
        {
            ck_assert_ptr_ne(argv[i], result[i]);
            ck_assert_str_eq(argv[i], result[i]);
            free(argv[i]);
        }
        ck_assert_ptr_null(result[num]);
        freeargv(result);
    }
    freeargv(NULL);
}
END_TEST

START_TEST(util_list)
{
    list_t* L = list_create();
    ck_assert_uint_eq(list_size(L), 0);
    ck_assert_ptr_null(list_get(L, 0));
    ck_assert_uint_eq(list_append(L, strdup("0")), true);
    ck_assert_uint_eq(list_size(L), 1);
    ck_assert_str_eq((char*)list_get(L, 0), "0");
    ck_assert_ptr_null(list_get(L, 1));
    ck_assert_uint_eq(list_append(L, strdup("1")), true);
    ck_assert_uint_eq(list_append(L, strdup("2")), true);
    ck_assert_uint_eq(list_append(L, strdup("3")), true);
    ck_assert_uint_eq(list_append(L, strdup("4")), true);
    ck_assert_uint_eq(list_append(L, strdup("5")), true);
    ck_assert_uint_eq(list_size(L), 6);
    ck_assert_str_eq((char*)list_get(L, 0), "0");
    ck_assert_str_eq((char*)list_get(L, 1), "1");
    ck_assert_str_eq((char*)list_get(L, 2), "2");
    ck_assert_str_eq((char*)list_get(L, 3), "3");
    ck_assert_str_eq((char*)list_get(L, 4), "4");
    ck_assert_str_eq((char*)list_get(L, 5), "5");
    ck_assert_ptr_null(list_get(L, 6));
    list_clear(L, free);
    ck_assert_uint_eq(list_size(L), 0);
    ck_assert_uint_eq(list_append(L, strdup("a")), true);
    ck_assert_str_eq((char*)list_get(L, 0), "a");
    list_destroy(L, free);
    list_destroy(NULL, free);
}
END_TEST

START_TEST(util_b32h_encode)
{
    char buffer[41];
    char* b32h;

    b32h = b32h_encode("", 0, buffer, sizeof(buffer));
    ck_assert_ptr_nonnull(b32h);
    ck_assert_str_eq(b32h, "");

    b32h = b32h_encode("-PostSRSd-", 10, buffer, sizeof(buffer));
    ck_assert_ptr_nonnull(b32h);
    ck_assert_str_eq(b32h, "5L86USRKAD956P1D");

    ck_assert_ptr_null(b32h_encode("BufferTooSmall!", 15, buffer, 24));

    b32h = b32h_encode("BuffLargeEnough", 15, buffer, 25);
    ck_assert_ptr_nonnull(b32h);
    ck_assert_str_eq(b32h, "89QMCPICC5P6EPA5DPNNAPR8");

    b32h = b32h_encode("a", 1, buffer, sizeof(buffer));
    ck_assert_ptr_nonnull(b32h);
    ck_assert_str_eq(b32h, "C4======");

    b32h = b32h_encode("ab", 2, buffer, sizeof(buffer));
    ck_assert_ptr_nonnull(b32h);
    ck_assert_str_eq(b32h, "C5H0====");

    b32h = b32h_encode("abc", 3, buffer, sizeof(buffer));
    ck_assert_ptr_nonnull(b32h);
    ck_assert_str_eq(b32h, "C5H66===");

    b32h = b32h_encode("abcd", 4, buffer, sizeof(buffer));
    ck_assert_ptr_nonnull(b32h);
    ck_assert_str_eq(b32h, "C5H66P0=");

    b32h = b32h_encode("abcde", 5, buffer, sizeof(buffer));
    ck_assert_ptr_nonnull(b32h);
    ck_assert_str_eq(b32h, "C5H66P35");

    b32h = b32h_encode("\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80", 10, buffer,
                       sizeof(buffer));
    ck_assert_ptr_nonnull(b32h);
    ck_assert_str_eq(b32h, "G2081040G2081040");
}
END_TEST

START_TEST(util_dotlock)
{
#if defined(LOCK_EX) && defined(LOCK_NB)
    for (int i = 0; i < 2; ++i)
    {
        int handle = acquire_lock("testfile");
        ck_assert_int_gt(handle, 0);
        ck_assert_int_lt(acquire_lock("testfile"), 0);
        release_lock("testfile", handle);
    }
#endif
}
END_TEST

START_TEST(util_domain_set)
{
    struct domain_set* D = domain_set_create();
    ck_assert(!domain_set_contains(D, "example.com"));
    ck_assert(!domain_set_contains(D, ".example.com"));
    ck_assert(!domain_set_contains(D, "exam.com"));
    domain_set_add(D, "example.com");
    domain_set_add(D, "www.example.com");
    ck_assert(domain_set_contains(D, "example.com"));
    ck_assert(domain_set_contains(D, "EXAMPLE.COM"));
    ck_assert(domain_set_contains(D, "www.example.com"));
    ck_assert(!domain_set_contains(D, ".example.com"));
    ck_assert(!domain_set_contains(D, "mail.example.com"));
    ck_assert(!domain_set_contains(D, "exam.com"));
    domain_set_add(D, ".example.com");
    ck_assert(domain_set_contains(D, "example.com"));
    ck_assert(domain_set_contains(D, ".example.com"));
    ck_assert(domain_set_contains(D, "www.example.com"));
    ck_assert(domain_set_contains(D, "mail.example.com"));
    ck_assert(!domain_set_contains(D, "exam.com"));
    domain_set_add(D, ".my-examples.com");
    ck_assert(!domain_set_contains(D, "my-examples.com"));
    ck_assert(domain_set_contains(D, "another.one.of.my-examples.com"));
    domain_set_add(D, "invalid$domain.net");
    ck_assert(!domain_set_contains(D, "invalid$domain.net"));
    domain_set_add(
        D, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-0123456789.");
    ck_assert(domain_set_contains(
        D, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-0123456789."));
    domain_set_destroy(D);
}

START_TEST(util_endpoint_for_milter)
{
    char* ep;
    ck_assert_ptr_null(endpoint_for_milter(NULL));
    ck_assert_ptr_null(endpoint_for_milter("invalid"));
    ck_assert_ptr_null(
        endpoint_for_milter("https://this.is.not.a.valid.endpoint.net"));
    ck_assert_ptr_null(endpoint_for_milter("inet:host.but.no.port"));
    ck_assert_ptr_null(endpoint_for_milter("inet:1234"));
    ck_assert_ptr_null(endpoint_for_milter("inet:localhost:"));
    ck_assert_ptr_null(endpoint_for_milter("inet::1234"));
    ck_assert_ptr_null(endpoint_for_milter("inet6:1234"));
    ck_assert_ptr_null(endpoint_for_milter("inet6:localhost:"));
    ck_assert_ptr_null(endpoint_for_milter("inet6::1234"));
    ep = endpoint_for_milter("unix:/some/path");
    ck_assert_str_eq(ep, "unix:/some/path");
    free(ep);
    ep = endpoint_for_milter("inet:localhost:1234");
    ck_assert_str_eq(ep, "inet:1234@localhost");
    free(ep);
    ep = endpoint_for_milter("inet:*:1234");
    ck_assert_str_eq(ep, "inet:1234");
    free(ep);
    ep = endpoint_for_milter("inet6:localhost:1234");
    ck_assert_str_eq(ep, "inet6:1234@localhost");
    free(ep);
    ep = endpoint_for_milter("inet6:*:1234");
    ck_assert_str_eq(ep, "inet6:1234");
    free(ep);
}
END_TEST

START_TEST(util_log)
{
    char buffer[2049];
    memset(buffer, 'a', sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = 0;
    log_enable_syslog();
    log_info("Hello %s", "World");
    log_warn("Excessively long message: %s", buffer);
    log_error("Error?");
}
END_TEST

BEGIN_TEST_SUITE(util)
ADD_TEST_CASE_WITH_UNCHECKED_FIXTURE(fs, setup_fs, teardown_fs)
ADD_TEST_TO_TEST_CASE(fs, util_file_exists)
ADD_TEST_TO_TEST_CASE(fs, util_directory_exists)
ADD_TEST_TO_TEST_CASE(fs, util_dotlock)
ADD_TEST(util_set_string)
ADD_TEST(util_argvdup);
ADD_TEST(util_list);
ADD_TEST(util_b32h_encode)
ADD_TEST(util_domain_set)
ADD_TEST(util_endpoint_for_milter)
ADD_TEST(util_log)
END_TEST_SUITE()
TEST_MAIN(util)
