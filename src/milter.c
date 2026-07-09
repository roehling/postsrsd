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
#include <sys/uio.h>

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

size_t milter_receive(int fd, void* buffer, size_t size, size_t* truncated)
{
    static char discardpile[512];
    uint32_t len;
    if (truncated != NULL)
        *truncated = 0;
    if (read(fd, &len, 4) != 4)
        return 0;
    len = BE32(len);
    size_t read_len = len < size ? len : size;
    size_t total_read = 0;
    while (total_read < read_len)
    {
        ssize_t r = read(fd, buffer + total_read, read_len - total_read);
        if (r < 0)
            return 0;
        total_read += r;
    }
    len -= total_read;
    while (len > 0)
    {
        read_len = len < sizeof(discardpile) ? len : sizeof(discardpile);
        ssize_t r = read(fd, discardpile, read_len);
        if (r < 0)
            break;
        len -= r;
        if (truncated != NULL)
            *truncated += r;
    }
    return total_read;
}

bool milter_send_bytes(int fd, char action, const void* data, size_t length)
{
    if (data == NULL && length != 0)
        return false;
    if (length > 0xfffffffe)
        return false;
    milter_packet_t packet;
    struct iovec iov[2];
    packet.length = BE32(1 + length);
    packet.code = action;
    iov[0].iov_base = &packet;
    iov[0].iov_len = sizeof(milter_packet_t);
    if (length != 0)
    {
        iov[1].iov_base = (void*)data;
        iov[1].iov_len = length;
    }
    return writev_all(fd, iov, length != 0 ? 2 : 1);
}

bool milter_send_str(int fd, char action, const char* value)
{
    return milter_send_bytes(fd, action, value,
                             value != NULL ? strlen(value) + 1 : 0);
}

bool milter_send(int fd, char action)
{
    milter_packet_t packet;
    packet.length = BE32(1);
    packet.code = action;
    return write(fd, &packet, sizeof(milter_packet_t))
           == sizeof(milter_packet_t);
}

bool milter_continue(int fd)
{
    return milter_send(fd, MILTER_DO_CONTINUE);
}

bool milter_tempfail(int fd)
{
    return milter_send(fd, MILTER_DO_TEMPFAIL);
}

bool milter_accept(int fd)
{
    return milter_send(fd, MILTER_DO_ACCEPT);
}

bool milter_reject(int fd)
{
    return milter_send(fd, MILTER_DO_REJECT);
}

bool milter_send_str_list(int fd, char action, list_t* L)
{
    size_t numv = list_size(L);
    if (numv == 0)
        return milter_send(fd, action);
    struct iovec iov[numv + 1];
    size_t j = 1, length = 0;
    for (size_t i = 0; i < numv; ++i)
    {
        const char* item = list_get(L, i);
        if (item != NULL)
        {
            size_t item_length = strlen(item) + 1;
            if (length > 0xfffffffe - item_length)
                return false;
            length += item_length;
            iov[j].iov_base = (void*)item;
            iov[j].iov_len = item_length;
            ++j;
        }
    }
    if (length > 0xfffffffe)
        return false;
    milter_packet_t packet;
    packet.length = BE32(1 + length);
    packet.code = action;
    iov[0].iov_base = &packet;
    iov[0].iov_len = sizeof(milter_packet_t);
    return writev_all(fd, iov, j);
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

bool milter_handle_optneg(int fd, const void* input, size_t length)
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
        milter_send(fd, MILTER_DO_TEMPFAIL);
        return false;
    }
    if ((buf.actions & MILTER_FL_ADDRCPT) == 0)
    {
        log_error("MTA does not support ADDRCPT milter action");
        milter_send(fd, MILTER_DO_TEMPFAIL);
        return false;
    }
    if ((buf.actions & MILTER_FL_DELRCPT) == 0)
    {
        log_error("MTA does not support DELRCPT milter action");
        milter_send(fd, MILTER_DO_TEMPFAIL);
        return false;
    }
    buf.protocol &= (MILTER_FL_NOCONNECT | MILTER_FL_NOHELO | MILTER_FL_NOHDRS
                     | MILTER_FL_NOBODY | MILTER_FL_NODATA | MILTER_FL_NOUNKNOWN
                     | MILTER_FL_NOEOH);

    buf.version = BE32(buf.version);
    buf.actions = BE32(buf.actions);
    buf.protocol = BE32(buf.protocol);
    return milter_send_bytes(fd, MILTER_CMD_OPTNEG, &buf, sizeof(buf));
}

char* milter_parse_macros(const char* name, const char* buffer, size_t length)
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

char* milter_parse_address_n(const char* addr, size_t length)
{
    if (addr == NULL)
        return NULL;
    if (length < 2)
        return strndup(addr, length);
    const char* lbrak = memchr(addr, '<', length);
    if (lbrak == NULL)
    {
        if (memchr(addr, '>', length) == NULL)
            return strndup(addr, length);
        return NULL;
    }
    const char* rbrak = memchr(lbrak, '>', length - (lbrak - addr));
    if (rbrak == NULL)
        return NULL;
    return strndup(lbrak + 1, rbrak - lbrak - 1);
}

char* milter_parse_address(const char* addr)
{
    if (addr == NULL)
        return NULL;
    const char* lbrak = strchr(addr, '<');
    if (lbrak == NULL)
    {
        if (strchr(addr, '>') == NULL)
            return strdup(addr);
        return NULL;
    }
    const char* rbrak = strchr(lbrak, '>');
    if (rbrak == NULL)
        return NULL;
    return strndup(lbrak + 1, rbrak - lbrak - 1);
}

char* milter_parse_address_buf(const char* addr, void* buffer, size_t size)
{
    if (addr == NULL)
        return NULL;
    const char* lbrak = strchr(addr, '<');
    if (lbrak == NULL)
    {
        if (strchr(addr, '>') == NULL)
        {
            size_t len = strlen(addr);
            if (len < size)
            {
                char* eob = stpcpy(buffer, addr);
                *eob = 0;
                return buffer;
            }
        }
        return NULL;
    }
    const char* rbrak = strchr(lbrak, '>');
    if (rbrak == NULL)
        return NULL;
    size_t len = rbrak - lbrak - 1;
    if (len >= size)
        return NULL;
    char* eob = stpncpy(buffer, lbrak + 1, len);
    *eob = 0;
    return buffer;
}
