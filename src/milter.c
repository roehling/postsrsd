/* PostSRSd - Sender Rewriting Scheme daemon for Postfix
 * Copyright 2012-2023 Timo Röhling <timo@gaussglocke.de>
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
#include "milter.h"

#include "util.h"

#include <assert.h>
#include <postsrsd_build_config.h>
#include <stdint.h>
#include <string.h>

#ifdef HAVE_ERRNO_H
#    include <errno.h>
#endif

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#    define BE32(x) (x)
#else
#    ifdef __GNUC__
#        define BE32(x) __builtin_bswap32(x)
#    else
#        include <byteswap.h>
#        define BE32(x) bswap_32(x)
#    endif
#endif

size_t milter_receive(FILE* fp, void* buffer, size_t size, size_t* truncated)
{
    static char discardpile[512];
    uint32_t len;
    if (truncated != NULL)
        *truncated = 0;
    if (fread(&len, 4, 1, fp) != 1)
        return 0;
    len = BE32(len);
    size_t result = fread(buffer, 1, len < size ? len : size, fp);
    if (result == 0)
        return result;
    len -= result;
    while (len > 0)
    {
        size_t skip =
            fread(discardpile, 1,
                  len < sizeof(discardpile) ? len : sizeof(discardpile), fp);
        len -= skip;
        if (truncated != NULL)
            *truncated += skip;
        if (skip == 0)
            break;
    }
    return result;
}

bool milter_send_bytes(FILE* fp, char action, const void* buffer, size_t length)
{
    if (buffer == NULL && length != 0)
        return false;
    if (length > 0xfffffffe)
        return false;
    uint32_t packet_length = BE32(1 + length);
    if (fwrite(&packet_length, 4, 1, fp) != 1)
        return false;
    if (fwrite(&action, 1, 1, fp) != 1)
        return false;
    if (length > 0)
        return fwrite(buffer, 1, length, fp) == length;
    else
        return true;
}

bool milter_send_str(FILE* fp, char action, const char* value)
{
    size_t length = value != NULL ? strlen(value) : 0;
    if (length > 0xfffffffd)
        return false;
    if (value != NULL)
        ++length; /* include NUL byte */
    uint32_t packet_length = BE32(1 + length);
    if (fwrite(&packet_length, 4, 1, fp) != 1)
        return false;
    if (fwrite(&action, 1, 1, fp) != 1)
        return false;
    if (value != NULL)
        return fwrite(value, 1, length, fp) == length;
    else
        return true;
}

bool milter_send(FILE* fp, char action)
{
    uint32_t packet_length = BE32(1);
    if (fwrite(&packet_length, 4, 1, fp) != 1)
        return false;
    return fwrite(&action, 1, 1, fp) == 1;
}

bool milter_send_str_array(FILE* fp, char action, const char* const* value,
                           size_t count)
{
    uint32_t packet_length = 1;
    if (value != NULL)
    {
        for (size_t i = 0; i < count; ++i)
        {
            if (value[i] != NULL)
            {
                size_t len = strlen(value[i]) + 1;
                if (len > 0xffffffff - packet_length)
                    return false;
                packet_length += len;
            }
        }
    }
    packet_length = BE32(packet_length);
    if (fwrite(&packet_length, 4, 1, fp) != 1)
        return false;
    if (fwrite(&action, 1, 1, fp) != 1)
        return false;
    if (value != NULL)
    {
        for (size_t i = 0; i < count; ++i)
        {
            if (value[i] != NULL)
            {
                size_t len = strlen(value[i]) + 1;
                if (fwrite(value[i], 1, len, fp) != len)
                    return false;
            }
        }
    }
    return true;
}

bool milter_handle_optneg(FILE* fp, const void* input, size_t length)
{
    struct
    {
        uint32_t version, actions, protocol;
    } ATTRIBUTE(packed) buf;
    static_assert(sizeof(buf) == 12,
                  "internal struct has unexpected alignment");

    if (length < sizeof(buf))
        return false;
    memcpy(&buf, input, sizeof(buf));
    buf.version = BE32(buf.version);
    buf.actions = BE32(buf.actions);
    buf.protocol = BE32(buf.protocol);

    if (buf.version > 6)
    {
        log_error("unsupported milter protocol version %u", buf.version);
        milter_send(fp, MILTER_DO_TEMPFAIL);
        return false;
    }
    buf.actions &= MILTER_FL_CHGFROM | MILTER_FL_ADDRCPT | MILTER_FL_DELRCPT;
    if ((buf.actions & MILTER_FL_CHGFROM) == 0)
    {
        log_error("MTA does not support CHGFROM milter action");
        milter_send(fp, MILTER_DO_TEMPFAIL);
        return false;
    }
    if ((buf.actions & MILTER_FL_ADDRCPT) == 0)
    {
        log_error("MTA does not support ADDRCPT milter action");
        milter_send(fp, MILTER_DO_TEMPFAIL);
        return false;
    }
    if ((buf.actions & MILTER_FL_DELRCPT) == 0)
    {
        log_error("MTA does not support DELRCPT milter action");
        milter_send(fp, MILTER_DO_TEMPFAIL);
        return false;
    }
    buf.protocol &= (MILTER_FL_NOCONNECT | MILTER_FL_NOHELO | MILTER_FL_NOHDRS
                     | MILTER_FL_NOBODY | MILTER_FL_NODATA | MILTER_FL_NOUNKNOWN
                     | MILTER_FL_NOEOH);

    buf.version = BE32(buf.version);
    buf.actions = BE32(buf.actions);
    buf.protocol = BE32(buf.protocol);
    return milter_send_bytes(fp, MILTER_CMD_OPTNEG, &buf, sizeof(buf));
}

char* milter_find_macro(const char* name, const char* buffer, size_t length)
{
    while (length > 0)
    {
        const char* key = buffer;
        size_t keylen = strnlen(key, length);
        if (length == keylen)
            return NULL;
        length -= keylen + 1;
        buffer += keylen + 1;
        if (strcmp(key, name) == 0)
            return strndup(buffer, length);
        size_t vallen = strnlen(buffer, length);
        if (length == vallen)
            return NULL;
        length -= vallen + 1;
        buffer += vallen + 1;
    }
    return NULL;
}
