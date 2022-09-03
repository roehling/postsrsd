/* PostSRSd - Sender Rewriting Scheme daemon for Postfix
 * Copyright Gisle Ass, Peter C. Gutmann, Bruce Schneier
 * Copyright 2004 Shevek <srs@anarres.org>
 * Copyright 2012-2022 Timo Röhling <timo@gaussglocke.de>
 * SPDX-License-Identifier: GPL-2.0-only OR BSD-3-Clause
 */
#ifndef SHA1_H
#define SHA1_H

typedef unsigned long ULONG; /* 32-or-more-bit quantity */
typedef unsigned char sha_byte;

#define SHA_BLOCKSIZE  64
#define SHA_DIGESTSIZE 20

typedef struct
{
    ULONG digest[5];              /* message digest */
    ULONG count_lo, count_hi;     /* 64-bit bit count */
    sha_byte data[SHA_BLOCKSIZE]; /* SHA data buffer */
    int local;                    /* unprocessed amount in data */
} SHA_INFO;

typedef struct _srs_hmac_ctx_t
{
    SHA_INFO sctx;
    char ipad[SHA_BLOCKSIZE + 1];
    char opad[SHA_BLOCKSIZE + 1];
} srs_hmac_ctx_t;

void sha_digest(char* out, char* data, unsigned len);
void srs_hmac_init(srs_hmac_ctx_t* ctx, char* secret, unsigned len);
void srs_hmac_update(srs_hmac_ctx_t* ctx, char* data, unsigned len);
void srs_hmac_fini(srs_hmac_ctx_t* ctx, char* out);

#endif
