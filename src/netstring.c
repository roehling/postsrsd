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
#include "netstring.h"

#include "util.h"

#include <stdint.h>
#include <string.h>
#include <sys/uio.h>

char* netstring_encode(const char* data, size_t length, char* buffer,
                       size_t bufsize, size_t* encoded_length)
{
    if (data == NULL)
        return NULL;
    int i = snprintf(buffer, bufsize, "%zu:", length);
    if (i <= 0 || length >= bufsize - i)
        return NULL;
    strncpy(&buffer[i], data, length);
    buffer[length + i] = ',';
    if (encoded_length != NULL)
        *encoded_length = length + i + 1;
    return buffer;
}

char* netstring_decode(const char* netstring, char* buffer, size_t bufsize,
                       size_t* decoded_length)
{
    if (netstring == NULL)
        return NULL;
    int i = -1;
    size_t length;
    if (sscanf(netstring, "%5zu%n", &length, &i) < 1)
        return NULL;
    if (i < 0 || length >= bufsize)
        return NULL;
    if (netstring[i] != ':' || netstring[length + i + 1] != ',')
        return NULL;
    strncpy(buffer, &netstring[i + 1], length);
    if (decoded_length != NULL)
        *decoded_length = length;
    buffer[length] = 0;
    return buffer;
}

char* netstring_read(int fd, char* buffer, size_t bufsize,
                     size_t* decoded_length)
{
    size_t length = 0;
    char ch;
    if (decoded_length != NULL)
        *decoded_length = 0;
    for (;;)
    {
        if (!read_all(fd, &ch, 1))
            return NULL;
        if (ch == ':')
            break;
        if (ch < '0' || ch > '9')
        {
            if (decoded_length != NULL)
                *decoded_length = SIZE_MAX;
            return NULL;
        }
        length = 10 * length + (ch - '0');
        if (length > 100000 || length >= bufsize)
        {
            if (decoded_length != NULL)
                *decoded_length = SIZE_MAX;
            return NULL;
        }
    }
    if (!read_all(fd, buffer, length))
        return NULL;
    if (!read_all(fd, &ch, 1))
        return NULL;
    if (ch != ',')
    {
        if (decoded_length != NULL)
            *decoded_length = SIZE_MAX;
        return NULL;
    }
    buffer[length] = 0;
    if (decoded_length != NULL)
        *decoded_length = length;
    return buffer;
}

int netstring_write(int fd, const char* data, size_t length)
{
    static const char suffix[] = {','};
    char prefix[16];
    struct iovec iov[3];
    if (length != 0)
    {
        if (data == NULL)
            return -1;
        size_t n = snprintf(prefix, sizeof(prefix), "%zu:", length);
        iov[0].iov_base = (void*)prefix;
        iov[0].iov_len = n;
        iov[1].iov_base = (void*)data;
        iov[1].iov_len = length;
        iov[2].iov_base = (void*)suffix;
        iov[2].iov_len = 1;
        return writev_all(fd, iov, 3) ? (int)(length + n + 1) : -1;
    }
    else
    {
        iov[0].iov_base = (void*)"0:,";
        iov[0].iov_len = 3;
        return writev_all(fd, iov, 1) ? 3 : -1;
    }
}
