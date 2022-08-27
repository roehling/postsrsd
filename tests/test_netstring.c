#include "netstring.h"

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

START_TEST(netstring_encode_test)
{
    char buffer[16];
    char* result;
    size_t length;

    result = netstring_encode("PostSRSd", 8, buffer, sizeof(buffer), &length);
    ck_assert_ptr_nonnull(result);
    ck_assert_uint_eq(length, 11);
    ck_assert_mem_eq(result, "8:PostSRSd,", length);

    result = netstring_encode("ItBarelyFits", 12, buffer, sizeof(buffer), &length);
    ck_assert_ptr_nonnull(result);
    ck_assert_uint_eq(length, 16);
    ck_assert_mem_eq(result, "12:ItBarelyFits,", length);

    result = netstring_encode("ItDoesNotFit!", 13, buffer, sizeof(buffer), &length);
    ck_assert_ptr_null(result);

    result = netstring_encode(NULL, 0, buffer, sizeof(buffer), &length);
    ck_assert_ptr_null(result);

    result = netstring_encode("", 0, buffer, sizeof(buffer), &length);
    ck_assert_ptr_nonnull(result);
    ck_assert_uint_eq(length, 3);
    ck_assert_mem_eq(result, "0:,", length);
}
END_TEST

START_TEST(netstring_decode_test)
{
    char buffer[16];
    char* result;
    size_t length;

    result = netstring_decode("8:PostSRSd,", buffer, sizeof(buffer), &length);
    ck_assert_ptr_nonnull(result);
    ck_assert_uint_eq(length, 8);
    ck_assert_mem_eq(result, "PostSRSd", length);

    result = netstring_decode("16:0123456789abcdef,", buffer, sizeof(buffer), &length);
    ck_assert_ptr_nonnull(result);
    ck_assert_uint_eq(length, 16);
    ck_assert_mem_eq(result, "0123456789abcdef", length);

    result = netstring_decode("0:,", buffer, sizeof(buffer), &length);
    ck_assert_ptr_nonnull(result);
    ck_assert_uint_eq(length, 0);

    result = netstring_decode(NULL, buffer, sizeof(buffer), &length);
    ck_assert_ptr_null(result);

    result = netstring_decode("1a,", buffer, sizeof(buffer), &length);
    ck_assert_ptr_null(result);

    result = netstring_decode("1:a*", buffer, sizeof(buffer), &length);
    ck_assert_ptr_null(result);

    result = netstring_decode("0x1:a,", buffer, sizeof(buffer), &length);
    ck_assert_ptr_null(result);

    result = netstring_decode("000001:a,", buffer, sizeof(buffer), &length);
    ck_assert_ptr_null(result);
}
END_TEST

START_TEST(netstring_io_test)
{
    int written;
    char* data;
    char buffer[16];
    size_t length;
    FILE* f = tmpfile();

    written = netstring_write(f, "PostSRSd", 8);
    ck_assert_int_eq(written, 11);
    written = netstring_write(f, "", 0);
    ck_assert_int_eq(written, 3);
    written = netstring_write(f, "0123456789abcdefgh", 17);
    ck_assert_int_eq(written, 21);

    fseek(f, 0, SEEK_SET);

    data = netstring_read(f, buffer, sizeof(buffer), &length);
    ck_assert_ptr_nonnull(data);
    ck_assert_uint_eq(length, 8);
    ck_assert_mem_eq(data, "PostSRSd", length);

    data = netstring_read(f, buffer, sizeof(buffer), &length);
    ck_assert_ptr_nonnull(data);
    ck_assert_uint_eq(length, 0);

    data = netstring_read(f, buffer, sizeof(buffer), &length);
    ck_assert_ptr_null(data);

    fseek(f, 0, SEEK_SET);
    ftruncate(fileno(f), 0);
    fwrite("3:abc,4:abcde", 1, 13, f);
    
    fseek(f, 0, SEEK_SET);
    data = netstring_read(f, buffer, sizeof(buffer), &length);
    ck_assert_ptr_nonnull(data);
    ck_assert_uint_eq(length, 3);
    ck_assert_mem_eq(data, "abc", length);

    data = netstring_read(f, buffer, sizeof(buffer), &length);
    ck_assert_ptr_null(data);

    fseek(f, 0, SEEK_SET);
    ftruncate(fileno(f), 0);
    fwrite("999:obviously too short,", 1, 4, f);

    fseek(f, 0, SEEK_SET);
    data = netstring_read(f, buffer, sizeof(buffer), &length);
    ck_assert_ptr_null(data);
}
END_TEST

static Suite* netstring_suite()
{
    Suite* s = suite_create("netstring");
    TCase* tc = tcase_create("test");
    tcase_add_test(tc, netstring_encode_test);
    tcase_add_test(tc, netstring_decode_test);
    tcase_add_test(tc, netstring_io_test);
    suite_add_tcase(s, tc);
    return s;
}

int main(int, char**)
{
    Suite* s = netstring_suite();
    SRunner* sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    int number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
