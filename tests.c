/* PostSRSd - Sender Rewriting Scheme daemon for Postfix
 * Copyright (c) 2012 Timo Röhling <timo.roehling@gmx.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* This program uses the libsrs2 library. The relevant source
 * files have been added to this distribution. */
#include "srs2.h"
#include <string.h>

static int run_srs(srs_t* srs, const char* address, const char* domain)
{
  int result, i;
  char buf1[1024];
  char buf2[1024];

  printf("srs_forward(\"%s\", \"%s\") = ", address, domain);
  result = srs_forward(srs, buf1, sizeof(buf1), address, domain);
  printf ("%d\n", result);
  if (result != SRS_SUCCESS) return 0;
  printf("srs_reverse(\"%s\") = ", buf1);
  result = srs_reverse(srs, buf2, sizeof(buf2), buf1);
  printf("%d\n", result);
  if (result != SRS_SUCCESS) return 0;
  if (strcasecmp(address, buf2))
  {
    printf("SRS not idempotent: \"%s\" != \"%s\"\n", address, buf2);
    return 0;
  }

  i = strchr(buf1, '@') - buf1;
  while (i > 0)
  {
    --i;
    if (buf1[i] == '=' || buf1[i] == '-' || buf1[i] == '+') continue;
    buf1[i]++;
    printf("srs_reverse(\"%s\") = ", buf1);
    result = srs_reverse(srs, buf2, sizeof(buf2), buf1);
    printf("%d\n", result);
    if (result == SRS_SUCCESS) return 0;
    buf1[i]--;
  }
  return 1;
}

static void generate_random_address(char* buf, size_t len1, size_t len2)
{
  static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789-+=";
  size_t i = 0, l1 = len1, l2 = len2;
  while (l1 > 0)
  {
    buf[i++] = chars[random() % 39];
    if (l1 < len1 && l1 > 1 && buf[i - 1] != '.' && (random() % 16 == 0)) buf[i - 1] = '.';
    --l1;
  }
  buf[i++] = '@';
  while (l2 > 0)
  {
    buf[i++] = 'a' + random() % 26;
    if (l2 < len2 && l2 > 1 && buf[i - 1] != '.' && (random() % 16 == 0)) buf[i - 1] = '.';
    --l2;
  }
  buf[i++] = 0;
}

#define ASSERT_SRS_OK(...) if (!run_srs(srs, __VA_ARGS__)) return EXIT_FAILURE

int main (int argc, char** argv)
{
  srs_t* srs;
  size_t l1, l2;
  char addr[128];

  srs = srs_new();
  srs_add_secret (srs, "t0ps3cr3t");

  for (l1 = 1; l1 <= 63; ++l1)
  {
    for (l2 = 1; l2 <= 63; ++l2)
    {
      generate_random_address(addr, l1, l2);
      ASSERT_SRS_OK(addr, "example.com");
    }
  }
  return EXIT_SUCCESS;
}

