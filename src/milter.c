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
#include "milter.h"

#include "util.h"

#include <assert.h>
#include <postsrsd_build_config.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

struct milter_packet
{
    uint32_t length;
    char code;
    char payload[];
} ATTRIBUTE(packed);
typedef struct milter_packet milter_packet_t;
static_assert(sizeof(struct milter_packet) == 5,
              "unexpected size of struct milter_packet");

size_t milter_receive(FILE* fp, void* buffer, size_t size, size_t* truncated)
{
    static char discardpile[512];
    uint32_t len;
    if (truncated != NULL)
        *truncated = 0;
    if (fread(&len, 4, 1, fp) != 1)
        return 0;
    len = BE32(len);
    size_t read_len = len < size ? len : size;
    size_t result = fread(buffer, 1, read_len, fp);
    if (result < read_len)
        return result;
    len -= result;
    while (len > 0)
    {
        read_len = len < sizeof(discardpile) ? len : sizeof(discardpile);
        size_t skip = fread(discardpile, 1, read_len, fp);
        len -= skip;
        if (truncated != NULL)
            *truncated += skip;
        if (skip < read_len)
            break;
    }
    return result;
}

bool milter_send_bytes(FILE* fp, char action, const void* data, size_t length)
{
    static char buffer[MILTER_PAYLOAD_SIZE + sizeof(milter_packet_t)];
    const bool allocate = length > sizeof(buffer) - sizeof(milter_packet_t);
    if (data == NULL && length != 0)
        return false;
    if (length > 0xfffffffe)
        return false;
    milter_packet_t* packet =
        allocate ? malloc(sizeof(milter_packet_t) + length) : &buffer;
    packet->length = BE32(1 + length);
    packet->code = action;
    if (length > 0)
        memcpy(packet->payload, data, length);
    size_t written = fwrite(packet, 1, sizeof(milter_packet_t) + length, fp);
    if (allocate)
        free(packet);
    return written == sizeof(milter_packet_t) + length;
}

bool milter_send_str(FILE* fp, char action, const char* value)
{
    return milter_send_bytes(fp, action, value,
                             value != NULL ? strlen(value) + 1 : 0);
}

bool milter_send(FILE* fp, char action)
{
    milter_packet_t packet;
    packet.length = BE32(1);
    packet.code = action;
    return fwrite(&packet, 1, sizeof(milter_packet_t), fp)
           == sizeof(milter_packet_t);
}

bool milter_continue(FILE* fp)
{
    return milter_send(fp, MILTER_DO_CONTINUE);
}

bool milter_tempfail(FILE* fp)
{
    return milter_send(fp, MILTER_DO_TEMPFAIL);
}

bool milter_accept(FILE* fp)
{
    return milter_send(fp, MILTER_DO_ACCEPT);
}

bool milter_reject(FILE* fp)
{
    return milter_send(fp, MILTER_DO_REJECT);
}

bool milter_send_str_list(FILE* fp, char action, list_t* L)
{
    static char buffer[MILTER_PAYLOAD_SIZE + sizeof(milter_packet_t)];
    if (list_size(L) == 0)
        return milter_send(fp, action);
    size_t length = 0;
    for (size_t i = 0; i < list_size(L); ++i)
    {
        const char* item = list_get(L, i);
        if (item != NULL)
        {
            size_t item_length = strlen(item) + 1;
            if (length > 0xfffffffe - item_length)
                return false;
            length += item_length;
        }
    }
    if (length > 0xfffffffe)
        return false;
    const bool allocate = length > sizeof(buffer) - sizeof(milter_packet_t);
    milter_packet_t* packet =
        allocate ? malloc(sizeof(milter_packet_t) + length) : &buffer;
    packet->length = BE32(1 + length);
    packet->code = action;
    char* out = packet->payload;
    for (size_t i = 0; i < list_size(L); ++i)
    {
        const char* item = list_get(L, i);
        if (item != NULL)
        {
            out = stpcpy(out, item);
            *out++ = 0;
        }
    }
    size_t written = fwrite(packet, 1, sizeof(milter_packet_t) + length, fp);
    if (allocate)
        free(packet);
    return written == sizeof(milter_packet_t) + length;
}

void milter_parse_str_list(list_t* L, const char* data, size_t length)
{
    while (length > 0)
    {
        size_t next_item_length = strnlen(data, length);
        list_append(L, strndup(data, next_item_length));
        if (next_item_length == length)
            break;
        data += next_item_length + 1;
        length -= next_item_length + 1;
    }
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
        buf.version = 6;
    }
    buf.actions &= (MILTER_FL_CHGFROM | MILTER_FL_ADDRCPT | MILTER_FL_DELRCPT);
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
        if (keylen == length || keylen + 1 == length)
            return NULL;
        length -= keylen + 1;
        buffer += keylen + 1;
        if (strcmp(key, name) == 0)
            return strndup(buffer, length);
        size_t vallen = strnlen(buffer, length);
        if (vallen == length || vallen + 1 == length)
            return NULL;
        length -= vallen + 1;
        buffer += vallen + 1;
    }
    return NULL;
}
