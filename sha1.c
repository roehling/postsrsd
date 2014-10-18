/* This file is copied from the libsrs2 sources */
/* Modified by Timo Röhling <timo.roehling@gmx.de> */

/* NIST Secure Hash Algorithm */
/* Borrowed from SHA1.xs by Gisle Ass */
/* heavily modified by Uwe Hollerbach <uh@alumni.caltech edu> */
/* from Peter C. Gutmann's implementation as found in */
/* Applied Cryptography by Bruce Schneier */
/* Further modifications to include the "UNRAVEL" stuff, below */
/* HMAC functions by Shevek <srs@anarres.org> for inclusion in
 * libsrs2, under GPL-2 or BSD license. Combine this lot in any way
 * you think will stand up in court. I hope my intent is clear. */

/* This code is in the public domain */

/* Useful defines & typedefs */

#include <stdarg.h>
#include <time.h>       /* time */
#include <sys/types.h>  /* tyepdefs */
#include <sys/time.h>   /* timeval / timezone struct */
#include <string.h>		/* memcpy, strcpy, memset */

#include "srs2.h"

#ifdef SIZEOF_UNSIGNED_LONG
#if SIZEOF_UNSIGNED_LONG < 4
#error "SHA1 requires an unsigned long of at least 32 bits"
#endif
#endif

#ifdef WORDS_BIGENDIAN
#define BYTEORDER 0x4321
#else
#define BYTEORDER 0x1234
#endif


/* UNRAVEL should be fastest & biggest */
/* UNROLL_LOOPS should be just as big, but slightly slower */
/* both undefined should be smallest and slowest */

#define SHA_VERSION 1
#define UNRAVEL
/* #define UNROLL_LOOPS */

/* SHA f()-functions */
#define f1(x,y,z)		((x & y) | (~x & z))
#define f2(x,y,z)		(x ^ y ^ z)
#define f3(x,y,z)		((x & y) | (x & z) | (y & z))
#define f4(x,y,z)		(x ^ y ^ z)

/* SHA constants */
#define CONST1				0x5a827999L
#define CONST2				0x6ed9eba1L
#define CONST3				0x8f1bbcdcL
#define CONST4				0xca62c1d6L

/* truncate to 32 bits -- should be a null op on 32-bit machines */
#define T32(x)		((x) & 0xffffffffL)

/* 32-bit rotate */
#define R32(x,n)		T32(((x << n) | (x >> (32 - n))))

/* the generic case, for when the overall rotation is not unraveled */
#define FG(n)		\
	T = T32(R32(A,5) + f##n(B,C,D) + E + *WP++ + CONST##n);		\
	E = D; D = C; C = R32(B,30); B = A; A = T

/* specific cases, for when the overall rotation is unraveled */
#define FA(n)		\
	T = T32(R32(A,5) + f##n(B,C,D) + E + *WP++ + CONST##n); B = R32(B,30)

#define FB(n)		\
	E = T32(R32(T,5) + f##n(A,B,C) + D + *WP++ + CONST##n); A = R32(A,30)

#define FC(n)		\
	D = T32(R32(E,5) + f##n(T,A,B) + C + *WP++ + CONST##n); T = R32(T,30)

#define FD(n)		\
	C = T32(R32(D,5) + f##n(E,T,A) + B + *WP++ + CONST##n); E = R32(E,30)

#define FE(n)		\
	B = T32(R32(C,5) + f##n(D,E,T) + A + *WP++ + CONST##n); D = R32(D,30)

#define FT(n)		\
	A = T32(R32(B,5) + f##n(C,D,E) + T + *WP++ + CONST##n); C = R32(C,30)


static void sha_transform(SHA_INFO *sha_info)
{
	int i;
	sha_byte *dp;
	ULONG T, A, B, C, D, E, W[80], *WP;

	dp = sha_info->data;

/*
the following makes sure that at least one code block below is
traversed or an error is reported, without the necessity for nested
preprocessor if/else/endif blocks, which are a great pain in the
nether regions of the anatomy...
*/
#undef SWAP_DONE

#if BYTEORDER == 0x1234
#define SWAP_DONE
	/* assert(sizeof(ULONG) == 4); */
	for (i = 0; i < 16; ++i) {
		T = *((ULONG *) dp);
		dp += 4;
		W[i] =  ((T << 24) & 0xff000000) | ((T <<  8) & 0x00ff0000) |
				((T >>  8) & 0x0000ff00) | ((T >> 24) & 0x000000ff);
	}
#endif

#if BYTEORDER == 0x4321
#define SWAP_DONE
	/* assert(sizeof(ULONG) == 4); */
	for (i = 0; i < 16; ++i) {
		T = *((ULONG *) dp);
		dp += 4;
		W[i] = T32(T);
	}
#endif

#if BYTEORDER == 0x12345678
#define SWAP_DONE
	/* assert(sizeof(ULONG) == 8); */
	for (i = 0; i < 16; i += 2) {
		T = *((ULONG *) dp);
		dp += 8;
		W[i] =  ((T << 24) & 0xff000000) | ((T <<  8) & 0x00ff0000) |
				((T >>  8) & 0x0000ff00) | ((T >> 24) & 0x000000ff);
		T >>= 32;
		W[i+1] = ((T << 24) & 0xff000000) | ((T <<  8) & 0x00ff0000) |
				 ((T >>  8) & 0x0000ff00) | ((T >> 24) & 0x000000ff);
	}
#endif

#if BYTEORDER == 0x87654321
#define SWAP_DONE
	/* assert(sizeof(ULONG) == 8); */
	for (i = 0; i < 16; i += 2) {
		T = *((ULONG *) dp);
		dp += 8;
		W[i] = T32(T >> 32);
		W[i+1] = T32(T);
	}
#endif

#ifndef SWAP_DONE
#error Unknown byte order -- you need to add code here
#endif /* SWAP_DONE */

	for (i = 16; i < 80; ++i) {
		W[i] = W[i-3] ^ W[i-8] ^ W[i-14] ^ W[i-16];
#if (SHA_VERSION == 1)
		W[i] = R32(W[i], 1);
#endif /* SHA_VERSION */
	}
	A = sha_info->digest[0];
	B = sha_info->digest[1];
	C = sha_info->digest[2];
	D = sha_info->digest[3];
	E = sha_info->digest[4];
	WP = W;
#ifdef UNRAVEL
	FA(1); FB(1); FC(1); FD(1); FE(1); FT(1); FA(1); FB(1); FC(1); FD(1);
	FE(1); FT(1); FA(1); FB(1); FC(1); FD(1); FE(1); FT(1); FA(1); FB(1);
	FC(2); FD(2); FE(2); FT(2); FA(2); FB(2); FC(2); FD(2); FE(2); FT(2);
	FA(2); FB(2); FC(2); FD(2); FE(2); FT(2); FA(2); FB(2); FC(2); FD(2);
	FE(3); FT(3); FA(3); FB(3); FC(3); FD(3); FE(3); FT(3); FA(3); FB(3);
	FC(3); FD(3); FE(3); FT(3); FA(3); FB(3); FC(3); FD(3); FE(3); FT(3);
	FA(4); FB(4); FC(4); FD(4); FE(4); FT(4); FA(4); FB(4); FC(4); FD(4);
	FE(4); FT(4); FA(4); FB(4); FC(4); FD(4); FE(4); FT(4); FA(4); FB(4);
	sha_info->digest[0] = T32(sha_info->digest[0] + E);
	sha_info->digest[1] = T32(sha_info->digest[1] + T);
	sha_info->digest[2] = T32(sha_info->digest[2] + A);
	sha_info->digest[3] = T32(sha_info->digest[3] + B);
	sha_info->digest[4] = T32(sha_info->digest[4] + C);
#else /* !UNRAVEL */
#ifdef UNROLL_LOOPS
	FG(1); FG(1); FG(1); FG(1); FG(1); FG(1); FG(1); FG(1); FG(1); FG(1);
	FG(1); FG(1); FG(1); FG(1); FG(1); FG(1); FG(1); FG(1); FG(1); FG(1);
	FG(2); FG(2); FG(2); FG(2); FG(2); FG(2); FG(2); FG(2); FG(2); FG(2);
	FG(2); FG(2); FG(2); FG(2); FG(2); FG(2); FG(2); FG(2); FG(2); FG(2);
	FG(3); FG(3); FG(3); FG(3); FG(3); FG(3); FG(3); FG(3); FG(3); FG(3);
	FG(3); FG(3); FG(3); FG(3); FG(3); FG(3); FG(3); FG(3); FG(3); FG(3);
	FG(4); FG(4); FG(4); FG(4); FG(4); FG(4); FG(4); FG(4); FG(4); FG(4);
	FG(4); FG(4); FG(4); FG(4); FG(4); FG(4); FG(4); FG(4); FG(4); FG(4);
#else /* !UNROLL_LOOPS */
	for (i =  0; i < 20; ++i) { FG(1); }
	for (i = 20; i < 40; ++i) { FG(2); }
	for (i = 40; i < 60; ++i) { FG(3); }
	for (i = 60; i < 80; ++i) { FG(4); }
#endif /* !UNROLL_LOOPS */
	sha_info->digest[0] = T32(sha_info->digest[0] + A);
	sha_info->digest[1] = T32(sha_info->digest[1] + B);
	sha_info->digest[2] = T32(sha_info->digest[2] + C);
	sha_info->digest[3] = T32(sha_info->digest[3] + D);
	sha_info->digest[4] = T32(sha_info->digest[4] + E);
#endif /* !UNRAVEL */
}

/* initialize the SHA digest */

static void
sha_init(SHA_INFO *sha_info)
{
	sha_info->digest[0] = 0x67452301L;
	sha_info->digest[1] = 0xefcdab89L;
	sha_info->digest[2] = 0x98badcfeL;
	sha_info->digest[3] = 0x10325476L;
	sha_info->digest[4] = 0xc3d2e1f0L;
	sha_info->count_lo = 0L;
	sha_info->count_hi = 0L;
	sha_info->local = 0;
}

/* update the SHA digest */

static void
sha_update(SHA_INFO *sha_info, sha_byte *buffer, int count)
{
	int i;
	ULONG clo;

	clo = T32(sha_info->count_lo + ((ULONG) count << 3));
	if (clo < sha_info->count_lo) {
		++sha_info->count_hi;
	}
	sha_info->count_lo = clo;
	sha_info->count_hi += (ULONG) count >> 29;
	if (sha_info->local) {
		i = SHA_BLOCKSIZE - sha_info->local;
		if (i > count) {
			i = count;
		}
		memcpy(((sha_byte *) sha_info->data) + sha_info->local, buffer, i);
		count -= i;
		buffer += i;
		sha_info->local += i;
		if (sha_info->local == SHA_BLOCKSIZE) {
			sha_transform(sha_info);
		} else {
			return;
		}
	}
	while (count >= SHA_BLOCKSIZE) {
		memcpy(sha_info->data, buffer, SHA_BLOCKSIZE);
		buffer += SHA_BLOCKSIZE;
		count -= SHA_BLOCKSIZE;
		sha_transform(sha_info);
	}
	memcpy(sha_info->data, buffer, count);
	sha_info->local = count;
}


static void
sha_transform_and_copy(unsigned char digest[20], SHA_INFO *sha_info)
{
	sha_transform(sha_info);
	digest[ 0] = (unsigned char) ((sha_info->digest[0] >> 24) & 0xff);
	digest[ 1] = (unsigned char) ((sha_info->digest[0] >> 16) & 0xff);
	digest[ 2] = (unsigned char) ((sha_info->digest[0] >>  8) & 0xff);
	digest[ 3] = (unsigned char) ((sha_info->digest[0]      ) & 0xff);
	digest[ 4] = (unsigned char) ((sha_info->digest[1] >> 24) & 0xff);
	digest[ 5] = (unsigned char) ((sha_info->digest[1] >> 16) & 0xff);
	digest[ 6] = (unsigned char) ((sha_info->digest[1] >>  8) & 0xff);
	digest[ 7] = (unsigned char) ((sha_info->digest[1]      ) & 0xff);
	digest[ 8] = (unsigned char) ((sha_info->digest[2] >> 24) & 0xff);
	digest[ 9] = (unsigned char) ((sha_info->digest[2] >> 16) & 0xff);
	digest[10] = (unsigned char) ((sha_info->digest[2] >>  8) & 0xff);
	digest[11] = (unsigned char) ((sha_info->digest[2]      ) & 0xff);
	digest[12] = (unsigned char) ((sha_info->digest[3] >> 24) & 0xff);
	digest[13] = (unsigned char) ((sha_info->digest[3] >> 16) & 0xff);
	digest[14] = (unsigned char) ((sha_info->digest[3] >>  8) & 0xff);
	digest[15] = (unsigned char) ((sha_info->digest[3]      ) & 0xff);
	digest[16] = (unsigned char) ((sha_info->digest[4] >> 24) & 0xff);
	digest[17] = (unsigned char) ((sha_info->digest[4] >> 16) & 0xff);
	digest[18] = (unsigned char) ((sha_info->digest[4] >>  8) & 0xff);
	digest[19] = (unsigned char) ((sha_info->digest[4]      ) & 0xff);
}

/* finish computing the SHA digest */
static void
sha_final(unsigned char digest[20], SHA_INFO *sha_info)
{
	int count;
	ULONG lo_bit_count, hi_bit_count;

	lo_bit_count = sha_info->count_lo;
	hi_bit_count = sha_info->count_hi;
	count = (int) ((lo_bit_count >> 3) & 0x3f);
	((sha_byte *) sha_info->data)[count++] = 0x80;
	if (count > SHA_BLOCKSIZE - 8) {
		memset(((sha_byte *) sha_info->data) + count, 0, SHA_BLOCKSIZE - count);
		sha_transform(sha_info);
		memset((sha_byte *) sha_info->data, 0, SHA_BLOCKSIZE - 8);
	} else {
		memset(((sha_byte *) sha_info->data) + count, 0,
			SHA_BLOCKSIZE - 8 - count);
	}
	sha_info->data[56] = (hi_bit_count >> 24) & 0xff;
	sha_info->data[57] = (hi_bit_count >> 16) & 0xff;
	sha_info->data[58] = (hi_bit_count >>  8) & 0xff;
	sha_info->data[59] = (hi_bit_count >>  0) & 0xff;
	sha_info->data[60] = (lo_bit_count >> 24) & 0xff;
	sha_info->data[61] = (lo_bit_count >> 16) & 0xff;
	sha_info->data[62] = (lo_bit_count >>  8) & 0xff;
	sha_info->data[63] = (lo_bit_count >>  0) & 0xff;
	sha_transform_and_copy(digest, sha_info);
}



/********************************************************************/

/*
		SHA_INFO ctx;
		unsigned char *data;
	STRLEN len;
		unsigned char digeststr[20];

		sha_init(&ctx);

		for (i = 0; i < items; i++) {
			data = (unsigned char *)(SvPVbyte(ST(i), len));
			sha_update(&ctx, data, len);
		}
		sha_final(digeststr, &ctx);
*/

static void
sha_digest(char *out, char *data, int len)
{
	SHA_INFO ctx;
	sha_init(&ctx);
	sha_update(&ctx, (sha_byte*)data, len);
	sha_final((sha_byte*)out, &ctx);
}

void
srs_hmac_init(srs_hmac_ctx_t *ctx, char *secret, int len)
{
	char	 sbuf[SHA_BLOCKSIZE];
	int		 i;

	if (len > SHA_BLOCKSIZE) {
		sha_digest(sbuf, secret, len);
		secret = sbuf;
		len = strlen(sbuf);	/* SHA_BLOCKSIZE? */
	}

	memset(ctx->ipad, 0x36, SHA_BLOCKSIZE);
	memset(ctx->opad, 0x5c, SHA_BLOCKSIZE);
	for (i = 0; i < len; i++) {
		ctx->ipad[i] ^= secret[i];
		ctx->opad[i] ^= secret[i];
	}

	memset(sbuf, 0, SHA_BLOCKSIZE);

	sha_init(&ctx->sctx);
	sha_update(&ctx->sctx, (sha_byte*)ctx->ipad, SHA_BLOCKSIZE);
}

void
srs_hmac_update(srs_hmac_ctx_t *ctx, char *data, int len)
{
	sha_update(&ctx->sctx, (sha_byte*)data, len);
}

void
srs_hmac_fini(srs_hmac_ctx_t *ctx, char *out)
{
	sha_byte buf[SHA_DIGESTSIZE + 1];

	sha_final(buf, &ctx->sctx);
	sha_init(&ctx->sctx);
	sha_update(&ctx->sctx, (sha_byte*)ctx->opad, SHA_BLOCKSIZE);
	sha_update(&ctx->sctx, buf, SHA_DIGESTSIZE);
	sha_final((sha_byte*)out, &ctx->sctx);
}
