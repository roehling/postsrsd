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
#include "netstring.h"

#include <string.h>

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
    if (encoded_length)
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
    if (decoded_length)
        *decoded_length = length;
    buffer[length] = 0;
    return buffer;
}

char* netstring_read(FILE* f, char* buffer, size_t bufsize,
                     size_t* decoded_length)
{
    size_t length;
    if (fscanf(f, "%5zu", &length) != 1)
        return NULL;
    if (fgetc(f) != ':')
        return NULL;
    if (length >= bufsize)
        return NULL;
    if (fread(buffer, 1, length, f) != length)
        return NULL;
    if (fgetc(f) != ',')
        return NULL;
    if (decoded_length)
        *decoded_length = length;
    buffer[length] = 0;
    return buffer;
}

int netstring_write(FILE* f, const char* data, size_t length)
{
    int i = fprintf(f, "%zu:", length);
    if (i < 0)
        return -1;
    if (fwrite(data, 1, length, f) != length)
        return -1;
    if (fputc(',', f) != ',')
        return -1;
    return length + i + 1;
}
