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
#include "sha1.h"

#include <check.h>
#include <stdlib.h>

START_TEST(sha1_test_vectors)
{
    char digest[20];
    sha_digest(digest, "", 0);
    ck_assert_mem_eq(digest,
                     "\xda\x39\xa3\xee\x5e\x6b\x4b\x0d\x32\x55\xbf\xef\x95\x60"
                     "\x18\x90\xaf\xd8\x07\x09",
                     20);
    sha_digest(digest, "abc", 3);
    ck_assert_mem_eq(digest,
                     "\xa9\x99\x3e\x36\x47\x06\x81\x6a\xba\x3e\x25\x71\x78\x50"
                     "\xc2\x6c\x9c\xd0\xd8\x9d",
                     20);
    char* one_million_a = (char*)malloc(1000000);
    ck_assert_ptr_nonnull(one_million_a);
    memset(one_million_a, 'a', 1000000);
    sha_digest(digest, one_million_a, 1000000);
    ck_assert_mem_eq(digest,
                     "\x34\xaa\x97\x3c\xd4\xc4\xda\xa4\xf6\x1e\xeb\x2b\xdb\xad"
                     "\x27\x31\x65\x34\x01\x6f",
                     20);
    free(one_million_a);
}
END_TEST

START_TEST(hmac_sha1_test_vectors)
{
    srs_hmac_ctx_t ctx;
    char digest[20];
    srs_hmac_init(&ctx, "topsecret", 9);
    srs_hmac_update(&ctx, "PostSRSd", 8);
    srs_hmac_fini(&ctx, digest);
    ck_assert_mem_eq(digest,
                     "\xc7\xaa\x15\x9e\x2d\x8f\x36\x6d\xe9\x50\xe5\xf9\x36\xdc"
                     "\x7f\x65\xea\x50\xd1\x13",
                     20);
    srs_hmac_init(&ctx, "Jefe", 4);
    srs_hmac_update(&ctx, "what do ya want for nothing?", 28);
    srs_hmac_fini(&ctx, digest);
    ck_assert_mem_eq(digest,
                     "\xef\xfc\xdf\x6a\xe5\xeb\x2f\xa2\xd2\x74\x16\xd5\xf1\x84"
                     "\xdf\x9c\x25\x9a\x7c\x79",
                     20);
    srs_hmac_init(
        &ctx,
        "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
        "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
        "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
        "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
        "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa",
        80);
    srs_hmac_update(&ctx,
                    "Test Using Larger Than Block-Size Key and Larger Than One "
                    "Block-Size Data",
                    73);
    srs_hmac_fini(&ctx, digest);
    ck_assert_mem_eq(digest,
                     "\xe8\xe9\x9d\x0f\x45\x23\x7d\x78\x6d\x6b\xba\xa7\x96\x5c"
                     "\x78\x08\xbb\xff\x1a\x91",
                     20);
}

BEGIN_TEST_SUITE(sha1)
ADD_TEST(sha1_test_vectors)
ADD_TEST(hmac_sha1_test_vectors)
END_TEST_SUITE()
TEST_MAIN(sha1)
