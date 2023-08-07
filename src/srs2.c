/* Copyright 2004 Shevek <srs@anarres.org>
 * Copyright 2012-2022 Timo Röhling <timo@gaussglocke.de>
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This file has been copied from libsrs2. Original copyright follows:
 */

/* Copyright (c) 2004 Shevek (srs@anarres.org)
 * All rights reserved.
 *
 * This file is a part of libsrs2 from http://www.libsrs2.org/
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, under the terms of either the GNU General Public
 * License version 2 or the BSD license, at the discretion of the
 * user. Copies of these licenses have been included in the libsrs2
 * distribution. See the the file called LICENSE for more
 * information.
 */

#include "srs2.h"

#include "sha1.h"

#include <postsrsd_build_config.h>
#include <stdarg.h>

#ifndef HAVE_STRCASECMP
#    ifdef HAVE__STRICMP
#        define strcasecmp _stricmp
#    endif
#endif

#ifndef HAVE_STRNCASECMP
#    ifdef HAVE__STRNICMP
#        define strncasecmp _strnicmp
#    endif
#endif

/* Use this */
#define STRINGP(s) ((s != NULL) && (*(s) != '\0'))

static const char* srs_separators = "=-+";

static srs_malloc_t srs_f_malloc = malloc;
static srs_realloc_t srs_f_realloc = realloc;
static srs_free_t srs_f_free = free;

int srs_set_malloc(srs_malloc_t m, srs_realloc_t r, srs_free_t f)
{
    srs_f_malloc = m;
    srs_f_realloc = r;
    srs_f_free = f;
    return SRS_SUCCESS;
}

const char* srs_strerror(int code)
{
    switch (code)
    {
        /* Simple errors */
        case SRS_SUCCESS:
            return "Success";
        case SRS_ENOTSRSADDRESS:
            return "Not an SRS address.";

        /* Config errors */
        case SRS_ENOSECRETS:
            return "No secrets in SRS configuration.";
        case SRS_ESEPARATORINVALID:
            return "Invalid separator suggested.";

        /* Input errors */
        case SRS_ENOSENDERATSIGN:
            return "No at sign in sender address";
        case SRS_EBUFTOOSMALL:
            return "Buffer too small.";

        /* Syntax errors */
        case SRS_ENOSRS0HOST:
            return "No host in SRS0 address.";
        case SRS_ENOSRS0USER:
            return "No user in SRS0 address.";
        case SRS_ENOSRS0HASH:
            return "No hash in SRS0 address.";
        case SRS_ENOSRS0STAMP:
            return "No timestamp in SRS0 address.";
        case SRS_ENOSRS1HOST:
            return "No host in SRS1 address.";
        case SRS_ENOSRS1USER:
            return "No user in SRS1 address.";
        case SRS_ENOSRS1HASH:
            return "No hash in SRS1 address.";
        case SRS_EBADTIMESTAMPCHAR:
            return "Bad base32 character in timestamp.";
        case SRS_EHASHTOOSHORT:
            return "Hash too short in SRS address.";

        /* SRS errors */
        case SRS_ETIMESTAMPOUTOFDATE:
            return "Time stamp out of date.";
        case SRS_EHASHINVALID:
            return "Hash invalid in SRS address.";

        default:
            return "Unknown error in SRS library.";
    }
}

srs_t* srs_new()
{
    srs_t* srs = (srs_t*)srs_f_malloc(sizeof(srs_t));
    srs_init(srs);
    return srs;
}

void srs_init(srs_t* srs)
{
    memset(srs, 0, sizeof(srs_t));
    srs->secrets = NULL;
    srs->numsecrets = 0;
    srs->separator = '=';
    srs->maxage = 21;
    srs->hashlength = 4;
    srs->hashmin = srs->hashlength;
    srs->alwaysrewrite = FALSE;
    srs->faketime = 0;
}

void srs_free(srs_t* srs)
{
    int i;
    for (i = 0; i < srs->numsecrets; i++)
    {
        memset(srs->secrets[i], 0, strlen(srs->secrets[i]));
        srs_f_free(srs->secrets[i]);
        srs->secrets[i] = 0;
    }
    srs_f_free(srs->secrets);
    srs_f_free(srs);
}

int srs_add_secret(srs_t* srs, const char* secret)
{
    int newlen = (srs->numsecrets + 1) * sizeof(char*);
    srs->secrets = (char**)srs_f_realloc(srs->secrets, newlen);
    srs->secrets[srs->numsecrets++] = strdup(secret);
    return SRS_SUCCESS;
}

const char* srs_get_secret(srs_t* srs, int idx)
{
    if (idx < srs->numsecrets)
        return srs->secrets[idx];
    return NULL;
}

#define SRS_PARAM_DEFINE(n, t)           \
    int srs_set_##n(srs_t* srs, t value) \
    {                                    \
        srs->n = value;                  \
        return SRS_SUCCESS;              \
    }                                    \
    t srs_get_##n(srs_t* srs)            \
    {                                    \
        return srs->n;                   \
    }

int srs_set_separator(srs_t* srs, char value)
{
    if (strchr(srs_separators, value) == NULL)
        return SRS_ESEPARATORINVALID;
    srs->separator = value;
    return SRS_SUCCESS;
}

char srs_get_separator(srs_t* srs)
{
    return srs->separator;
}

SRS_PARAM_DEFINE(maxage, int)
/* XXX Check hashlength >= hashmin */
SRS_PARAM_DEFINE(hashlength, int)
SRS_PARAM_DEFINE(hashmin, int)
SRS_PARAM_DEFINE(alwaysrewrite, srs_bool)
SRS_PARAM_DEFINE(noforward, srs_bool)
SRS_PARAM_DEFINE(noreverse, srs_bool)

/* Don't mess with these unless you know what you're doing well
 * enough to rewrite the timestamp functions. These are based on
 * a 2 character timestamp. Changing these in the wild is probably
 * a bad idea. */
#define SRS_TIME_PRECISION (60 * 60 * 24) /* One day */
#define SRS_TIME_BASEBITS  5              /* 2^5 = 32 = strlen(CHARS) */
/* This had better be a real variable since we do arithmethic
 * with it. */
const char* SRS_TIME_BASECHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
#define SRS_TIME_SIZE  2
#define SRS_TIME_SLOTS (1 << (SRS_TIME_BASEBITS << (SRS_TIME_SIZE - 1)))

int srs_timestamp_create(srs_t* srs __attribute__((unused)), char* buf,
                         time_t now)
{
    now = now / SRS_TIME_PRECISION;
    buf[1] = SRS_TIME_BASECHARS[now & ((1 << SRS_TIME_BASEBITS) - 1)];
    now = now >> SRS_TIME_BASEBITS;
    buf[0] = SRS_TIME_BASECHARS[now & ((1 << SRS_TIME_BASEBITS) - 1)];
    buf[2] = '\0';
    return SRS_SUCCESS;
}

int srs_timestamp_check(srs_t* srs, const char* stamp)
{
    const char* sp;
    char* bp;
    int off;
    time_t now;
    time_t then;

    if (strlen(stamp) != 2)
        return SRS_ETIMESTAMPOUTOFDATE;
    /* We had better go around this loop exactly twice! */
    then = 0;
    for (sp = stamp; *sp; sp++)
    {
        bp = strchr(SRS_TIME_BASECHARS, toupper(*sp));
        if (bp == NULL)
            return SRS_EBADTIMESTAMPCHAR;
        off = bp - SRS_TIME_BASECHARS;
        then = (then << SRS_TIME_BASEBITS) | off;
    }

    if (srs->faketime)
        now = srs->faketime;
    else
        time(&now);
    now = (now / SRS_TIME_PRECISION) % SRS_TIME_SLOTS;
    while (now < then)
        now = now + SRS_TIME_SLOTS;

    if (now <= then + srs->maxage)
        return SRS_SUCCESS;
    return SRS_ETIMESTAMPOUTOFDATE;
}

const char* SRS_HASH_BASECHARS =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

static void srs_hash_create_v(srs_t* srs, int idx, char* buf, int nargs,
                              va_list ap)
{
#ifdef USE_OPENSSL
    HMAC_CTX ctx;
    int srshashlen;
    char srshash[EVP_MAX_MD_SIZE + 1];
#else
    srs_hmac_ctx_t ctx;
    char srshash[SHA_DIGESTSIZE + 1];
#endif
    char* secret;
    char* data;
    int len;
    unsigned char* hp;
    char* bp;
    int i;
    int j;

    secret = srs->secrets[idx];

#ifdef USE_OPENSSL
    HMAC_CTX_init(&ctx);
    HMAC_Init(&ctx, secret, strlen(secret), EVP_sha1());
#else
    srs_hmac_init(&ctx, secret, strlen(secret));
#endif

    for (i = 0; i < nargs; i++)
    {
        data = va_arg(ap, char*);
        len = strlen(data);
        char lcdata[len + 1];
        for (j = 0; j < len; j++)
        {
            if (isupper(data[j]))
                lcdata[j] = tolower(data[j]);
            else
                lcdata[j] = data[j];
        }
#ifdef USE_OPENSSL
        HMAC_Update(&ctx, lcdata, len);
#else
        srs_hmac_update(&ctx, lcdata, len);
#endif
    }

#ifdef USE_OPENSSL
    HMAC_Final(&ctx, srshash, &srshashlen);
    HMAC_CTX_cleanup(&ctx);
    srshash[EVP_MAX_MD_SIZE] = '\0';
#else
    srs_hmac_fini(&ctx, srshash);
    srshash[SHA_DIGESTSIZE] = '\0';
#endif

    /* A little base64 encoding. Just a little. */
    hp = (unsigned char*)srshash;
    bp = buf;
    for (i = 0; i < srs->hashlength; i++)
    {
        switch (i & 0x03)
        {
            default: /* NOTREACHED */
            case 0:
                j = (*hp >> 2);
                break;
            case 1:
                j = ((*hp & 0x03) << 4) | ((*(hp + 1) & 0xF0) >> 4);
                hp++;
                break;
            case 2:
                j = ((*hp & 0x0F) << 2) | ((*(hp + 1) & 0xC0) >> 6);
                hp++;
                break;
            case 3:
                j = (*hp++ & 0x3F);
                break;
        }
        *bp++ = SRS_HASH_BASECHARS[j];
    }

    *bp = '\0';
    buf[srs->hashlength] = '\0';
}

int srs_hash_create(srs_t* srs, char* buf, int nargs, ...)
{
    va_list ap;

    if (srs->numsecrets == 0)
        return SRS_ENOSECRETS;
    if (srs->secrets == NULL)
        return SRS_ENOSECRETS;
    if (srs->secrets[0] == NULL)
        return SRS_ENOSECRETS;

    va_start(ap, nargs);
    srs_hash_create_v(srs, 0, buf, nargs, ap);
    va_end(ap);

    return SRS_SUCCESS;
}

int srs_hash_check(srs_t* srs, char* hash, int nargs, ...)
{
    va_list ap;
    int len;
    int i;

    len = strlen(hash);
    if (len < srs->hashmin)
        return SRS_EHASHTOOSHORT;
    if (len > srs->hashlength)
    {
        len = srs->hashlength;
    }

    char srshash[srs->hashlength + 1];
    for (i = 0; i < srs->numsecrets; i++)
    {
        va_start(ap, nargs);
        srs_hash_create_v(srs, i, srshash, nargs, ap);
        va_end(ap);
        if (strncasecmp(hash, srshash, len) == 0)
            return SRS_SUCCESS;
    }

    return SRS_EHASHINVALID;
}

int srs_compile_shortcut(srs_t* srs, char* buf, int buflen, char* sendhost,
                         char* senduser, const char* aliashost)
{
    char srsstamp[SRS_TIME_SIZE + 1];
    int len;
    int ret;

    /* This never happens if we get called from guarded() */
    if ((strncasecmp(senduser, SRS0TAG, 4) == 0)
        && (strchr(srs_separators, senduser[4]) != NULL))
    {
        sendhost = senduser + 5;
        if (*sendhost == '\0')
            return SRS_ENOSRS0HOST;
        senduser = strchr(sendhost, SRSSEP);
        if ((senduser == NULL) || (*senduser == '\0'))
            return SRS_ENOSRS0USER;
    }

    len = strlen(SRS0TAG) + 1 + srs->hashlength + 1 + SRS_TIME_SIZE + 1
          + strlen(sendhost) + 1 + strlen(senduser) + 1 + strlen(aliashost);
    if (len >= buflen)
        return SRS_EBUFTOOSMALL;

    ret = srs_timestamp_create(srs, srsstamp,
                               srs->faketime ? srs->faketime : time(NULL));
    if (ret != SRS_SUCCESS)
        return ret;
    char srshash[srs->hashlength + 1];
    ret = srs_hash_create(srs, srshash, 3, srsstamp, sendhost, senduser);
    if (ret != SRS_SUCCESS)
        return ret;

    sprintf(buf, SRS0TAG "%c%s%c%s%c%s%c%s@%s", srs->separator, srshash, SRSSEP,
            srsstamp, SRSSEP, sendhost, SRSSEP, senduser, aliashost);

    return SRS_SUCCESS;
}

int srs_compile_guarded(srs_t* srs, char* buf, int buflen, char* sendhost,
                        char* senduser, const char* aliashost)
{
    char* srshost;
    char* srsuser;
    int len;
    int ret;

    if ((strncasecmp(senduser, SRS1TAG, 4) == 0)
        && (strchr(srs_separators, senduser[4]) != NULL))
    {
        /* Used as a temporary convenience var */
        char* tmp = senduser + 5;
        if (*tmp == '\0')
            return SRS_ENOSRS1HASH;
        /* Used as a temporary convenience var */
        srshost = strchr(tmp, SRSSEP);
        if (!STRINGP(srshost))
            return SRS_ENOSRS1HOST;
        *srshost++ = '\0';
        srsuser = strchr(srshost, SRSSEP);
        if (!STRINGP(srsuser))
            return SRS_ENOSRS1USER;
        *srsuser++ = '\0';
        char srshash[srs->hashlength + 1];
        ret = srs_hash_create(srs, srshash, 2, srshost, srsuser);
        if (ret != SRS_SUCCESS)
            return ret;
        len = strlen(SRS1TAG) + 1 + srs->hashlength + 1 + strlen(srshost) + 1
              + strlen(srsuser) + 1 + strlen(aliashost);
        if (len >= buflen)
            return SRS_EBUFTOOSMALL;
        sprintf(buf, SRS1TAG "%c%s%c%s%c%s@%s", srs->separator, srshash, SRSSEP,
                srshost, SRSSEP, srsuser, aliashost);
        return SRS_SUCCESS;
    }
    else if ((strncasecmp(senduser, SRS0TAG, 4) == 0)
             && (strchr(srs_separators, senduser[4]) != NULL))
    {
        srsuser = senduser + 4;
        srshost = sendhost;
        char srshash[srs->hashlength + 1];
        ret = srs_hash_create(srs, srshash, 2, srshost, srsuser);
        if (ret != SRS_SUCCESS)
            return ret;
        len = strlen(SRS1TAG) + 1 + srs->hashlength + 1 + strlen(srshost) + 1
              + strlen(srsuser) + 1 + strlen(aliashost);
        if (len >= buflen)
            return SRS_EBUFTOOSMALL;
        sprintf(buf, SRS1TAG "%c%s%c%s%c%s@%s", srs->separator, srshash, SRSSEP,
                srshost, SRSSEP, srsuser, aliashost);
    }
    else
    {
        return srs_compile_shortcut(srs, buf, buflen, sendhost, senduser,
                                    aliashost);
    }

    return SRS_SUCCESS;
}

int srs_parse_shortcut(srs_t* srs, char* buf, unsigned buflen, char* senduser)
{
    char* srshash;
    char* srsstamp;
    char* srshost;
    char* srsuser;
    int ret;

    if (strncasecmp(senduser, SRS0TAG, 4) == 0)
    {
        srshash = senduser + 5;
        if (!STRINGP(srshash))
            return SRS_ENOSRS0HASH;
        srsstamp = strchr(srshash, SRSSEP);
        if (!STRINGP(srsstamp))
            return SRS_ENOSRS0STAMP;
        *srsstamp++ = '\0';
        srshost = strchr(srsstamp, SRSSEP);
        if (!STRINGP(srshost))
            return SRS_ENOSRS0HOST;
        *srshost++ = '\0';
        srsuser = strchr(srshost, SRSSEP);
        if (!STRINGP(srsuser))
            return SRS_ENOSRS0USER;
        *srsuser++ = '\0';
        ret = srs_timestamp_check(srs, srsstamp);
        if (ret != SRS_SUCCESS)
            return ret;
        ret = srs_hash_check(srs, srshash, 3, srsstamp, srshost, srsuser);
        if (ret != SRS_SUCCESS)
            return ret;
        snprintf(buf, buflen, "%s@%s", srsuser, srshost);
        return SRS_SUCCESS;
    }

    return SRS_ENOTSRSADDRESS;
}

int srs_parse_guarded(srs_t* srs, char* buf, int buflen, char* senduser)
{
    char* srshash;
    char* srshost;
    char* srsuser;
    int ret;

    if (strncasecmp(senduser, SRS1TAG, 4) == 0)
    {
        srshash = senduser + 5;
        if (!STRINGP(srshash))
            return SRS_ENOSRS1HASH;
        srshost = strchr(srshash, SRSSEP);
        if (!STRINGP(srshost))
            return SRS_ENOSRS1HOST;
        *srshost++ = '\0';
        srsuser = strchr(srshost, SRSSEP);
        if (!STRINGP(srsuser))
            return SRS_ENOSRS1USER;
        *srsuser++ = '\0';
        ret = srs_hash_check(srs, srshash, 2, srshost, srsuser);
        if (ret != SRS_SUCCESS)
            return ret;
        sprintf(buf, SRS0TAG "%s@%s", srsuser, srshost);
        return SRS_SUCCESS;
    }
    else
    {
        return srs_parse_shortcut(srs, buf, buflen, senduser);
    }
}

int srs_forward(srs_t* srs, char* buf, unsigned buflen, const char* sender,
                const char* alias)
{
    char* sendhost;
    char* tmp;
    unsigned len;

    if (srs->noforward)
        return SRS_ENOTREWRITTEN;

    /* This is allowed to be a plain domain */
    while ((tmp = strchr(alias, '@')) != NULL)
        alias = tmp + 1;

    tmp = strchr(sender, '@');
    if (tmp == NULL)
        return SRS_ENOSENDERATSIGN;
    sendhost = tmp + 1;

    len = strlen(sender);

    if (!srs->alwaysrewrite)
    {
        if (strcasecmp(sendhost, alias) == 0)
        {
            if (strlen(sender) >= buflen)
                return SRS_EBUFTOOSMALL;
            strcpy(buf, sender);
            return SRS_SUCCESS;
        }
    }

    char senduser[len + 1];
    strcpy(senduser, sender);
    tmp = (senduser + (tmp - sender));
    sendhost = tmp + 1;
    *tmp = '\0';

    return srs_compile_guarded(srs, buf, buflen, sendhost, senduser, alias);
}

int srs_forward_alloc(srs_t* srs, char** sptr, const char* sender,
                      const char* alias)
{
    char* buf;
    int slen;
    int alen;
    int len;
    int ret;

    if (srs->noforward)
        return SRS_ENOTREWRITTEN;

    slen = strlen(sender);
    alen = strlen(alias);

    /* strlen(SRSxTAG) + strlen("====+@") < 64 */
    len = slen + alen + srs->hashlength + SRS_TIME_SIZE + 64;
    buf = (char*)srs_f_malloc(len);

    ret = srs_forward(srs, buf, len, sender, alias);

    if (ret == SRS_SUCCESS)
        *sptr = buf;
    else
        srs_f_free(buf);

    return ret;
}

int srs_reverse(srs_t* srs, char* buf, unsigned buflen, const char* sender)
{
    char* tmp;
    unsigned len;

    if (!SRS_IS_SRS_ADDRESS(sender))
        return SRS_ENOTSRSADDRESS;

    if (srs->noreverse)
        return SRS_ENOTREWRITTEN;

    len = strlen(sender);
    if (len >= buflen)
        return SRS_EBUFTOOSMALL;
    char senduser[len + 1];
    strcpy(senduser, sender);

    /* We don't really care about the host for reversal. */
    tmp = strchr(senduser, '@');
    if (tmp != NULL)
        *tmp = '\0';
    return srs_parse_guarded(srs, buf, buflen, senduser);
}

int srs_reverse_alloc(srs_t* srs, char** sptr, const char* sender)
{
    char* buf;
    int len;
    int ret;

    *sptr = NULL;

    if (!SRS_IS_SRS_ADDRESS(sender))
        return SRS_ENOTSRSADDRESS;

    if (srs->noreverse)
        return SRS_ENOTREWRITTEN;

    len = strlen(sender) + 1;
    buf = (char*)srs_f_malloc(len);

    ret = srs_reverse(srs, buf, len, sender);

    if (ret == SRS_SUCCESS)
        *sptr = buf;
    else
        srs_f_free(buf);

    return ret;
}
