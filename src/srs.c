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
#include "srs.h"

#include "sha1.h"
#include "util.h"

#include <ctype.h>
#include <string.h>

char* postsrsd_forward(const char* addr, const char* domain, srs_t* srs,
                       database_t* db, domain_set_t* local_domains, bool* error,
                       const char** info)
{
    const char* at = strchr(addr, '@');
    if (error)
        *error = false;
    if (info)
        *info = NULL;
    if (!at)
    {
        if (info)
            *info = "No domain.";
        log_debug("<%s> not rewritten: no domain", addr);
        return NULL;
    }
    const char* input_domain = at + 1;
    if (domain_set_contains(local_domains, input_domain))
    {
        if (info)
            *info = "Need not rewrite local domain.";
        log_debug("<%s> not rewritten: local domain", addr);
        return NULL;
    }
    char db_alias_buf[35];
    char* db_alias;
    const char* sender = addr;
    if (db && !SRS_IS_SRS_ADDRESS(addr))
    {
        char digest[20];
        sha_digest(digest, addr, strlen(addr));
        db_alias = b32h_encode(digest, 20, db_alias_buf, sizeof(db_alias_buf));
        if (!db_alias)
        {
            log_warn("<%s> not rewritten: aliasing error", addr);
            if (error)
                *error = true;
            if (info)
                *info = "Aliasing error.";
            return NULL;
        }
        strcat(db_alias, "@1");
        if (!database_write(db, db_alias, addr, srs->maxage * 86400))
        {
            log_warn("<%s> not rewritten: database error", addr);
            if (error)
                *error = true;
            if (info)
                *info = "Database error.";
            return NULL;
        }
        sender = db_alias;
    }
    char* output = NULL;
    int result = srs_forward_alloc(srs, &output, sender, domain);
    if (result == SRS_SUCCESS)
    {
        log_info("<%s> forwarded as <%s>", addr, output);
        return output;
    }
    free(output);
    if (info)
        *info = srs_strerror(result);
    log_info("<%s> not rewritten: %s", addr, srs_strerror(result));
    return NULL;
}

char* postsrsd_reverse(const char* addr, srs_t* srs, database_t* db,
                       bool* error, const char** info)
{
    char buffer[513];
    if (error)
        *error = false;
    if (info)
        *info = NULL;
    int result = srs_reverse(srs, buffer, sizeof(buffer), addr);
    if (result != SRS_SUCCESS)
    {
        if (info)
            *info = srs_strerror(result);
        if (result != SRS_ENOTSRSADDRESS)
        {
            log_info("<%s> not reversed: %s", addr, srs_strerror(result));
        }
        else
        {
            log_debug("<%s> not reversed: %s", addr, srs_strerror(result));
        }
        return NULL;
    }
    const char* at = strchr(buffer, '@');
    if (!at)
    {
        log_info("<%s> not reversed: internal error", addr);
        if (error)
            *error = true;
        if (info)
            *info = "Internal error.";
        return NULL;
    }
    if (strcmp(at, "@1") == 0)
    {
        if (db)
        {
            char* p = buffer;
            while (*p)
            {
                *p = toupper(*p);
                ++p;
            }
            char* sender = database_read(db, buffer);
            if (!sender)
            {
                log_info("<%s> not reversed: unknown alias", addr);
                if (info)
                    *info = "Unknown alias.";
                return NULL;
            }
            log_info("<%s> reversed to <%s>", addr, sender);
            return sender;
        }
        else
        {
            log_warn("<%s> not reversed: no database for alias", addr);
            if (error)
                *error = true;
            if (info)
                *info = "No database for alias.";
            return NULL;
        }
    }
    log_info("<%s> reversed to <%s>", addr, buffer);
    return strdup(buffer);
}
