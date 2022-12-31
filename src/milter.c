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
#include "milter.h"

#include "database.h"
#include "postsrsd_build_config.h"
#include "srs.h"
#include "util.h"

#ifndef HAVE_STRNCASECMP
#    ifdef HAVE__STRNICMP
#        define strncasecmp _strnicmp
#    endif
#endif

#ifdef WITH_MILTER
#    include <libmilter/mfapi.h>
#    ifdef HAVE_UNISTD_H
#        include <unistd.h>
#    endif
#    include <string.h>

static char* milter_uri = NULL;
static char* milter_path = NULL;
static int milter_lock = -1;

static cfg_t* g_cfg = NULL;
static srs_t* g_srs = NULL;
static domain_set_t* g_local_domains = NULL;
static const char* g_srs_domain = NULL;

struct privdata
{
    char* envfrom;
    list_t* envrcpt;
};
typedef struct privdata privdata_t;
#endif

bool milter_create(const char* uri)
{
#ifdef WITH_MILTER
    milter_uri = endpoint_for_milter(uri);
    if (milter_uri == NULL)
    {
        log_error("invalid milter endpoint: %s", uri);
        return false;
    }
    return true;
#else
    MAYBE_UNUSED(uri);
    log_error("no milter support");
    return false;
#endif
}

#ifdef WITH_MILTER
static sfsistat on_connect(SMFICTX* ctx, char* hostname, _SOCK_ADDR* hostaddr)
{
    MAYBE_UNUSED(hostname);
    MAYBE_UNUSED(hostaddr);
    privdata_t* priv = malloc(sizeof(privdata_t));
    if (priv == NULL)
        return SMFIS_TEMPFAIL;
    priv->envfrom = NULL;
    priv->envrcpt = list_create();
    if (priv->envrcpt == NULL)
        return SMFIS_TEMPFAIL;
    smfi_setpriv(ctx, priv);
    return SMFIS_CONTINUE;
}

static sfsistat on_envfrom(SMFICTX* ctx, char** argv)
{
    privdata_t* priv = smfi_getpriv(ctx);
    char* from = strip_brackets(argv[0]);
    if (!from)
        return SMFIS_TEMPFAIL;
    priv->envfrom = from;
    return SMFIS_CONTINUE;
}

static sfsistat on_envrcpt(SMFICTX* ctx, char** argv)
{
    privdata_t* priv = smfi_getpriv(ctx);
    char* rcpt = strip_brackets(argv[0]);
    if (!rcpt)
        return SMFIS_TEMPFAIL;
    if (!list_append(priv->envrcpt, rcpt))
    {
        free(rcpt);
        return SMFIS_TEMPFAIL;
    }
    return SMFIS_CONTINUE;
}

static sfsistat on_eom(SMFICTX* ctx)
{
    database_t* db = NULL;
    if (cfg_getint(g_cfg, "original-envelope") == SRS_ENVELOPE_DATABASE)
    {
        db = database_connect(cfg_getstr(g_cfg, "envelope-database"), false);
        if (!db)
            return SMFIS_TEMPFAIL;
    }
    privdata_t* priv = smfi_getpriv(ctx);
    size_t rcpt_size = list_size(priv->envrcpt);
    bool error = false;
    for (size_t i = 0; i < rcpt_size; ++i)
    {
        char* rcpt = (char*)list_get(priv->envrcpt, i);
        char* rewritten = postsrsd_reverse(rcpt, g_srs, db, &error, NULL);
        if (error)
            return SMFIS_TEMPFAIL;
        if (rewritten)
        {
            char* bracketed_old_rcpt = add_brackets(rcpt);
            char* bracketed_new_rcpt = add_brackets(rewritten);
            free(rewritten);
            if (smfi_delrcpt(ctx, bracketed_old_rcpt) != MI_SUCCESS)
            {
                free(bracketed_old_rcpt);
                free(bracketed_new_rcpt);
                return SMFIS_TEMPFAIL;
            }
            if (smfi_addrcpt(ctx, bracketed_new_rcpt)
                != MI_SUCCESS)  // TODO maybe add ESMTP arguments?
            {
                free(bracketed_old_rcpt);
                free(bracketed_new_rcpt);
                return SMFIS_TEMPFAIL;
            }
            free(bracketed_old_rcpt);
            free(bracketed_new_rcpt);
        }
    }
    if (*priv->envfrom)
    {
        // TODO check if mail is actually forwarded
        char* rewritten = postsrsd_forward(priv->envfrom, g_srs_domain, g_srs,
                                           db, g_local_domains, &error, NULL);
        if (error)
            return SMFIS_TEMPFAIL;
        if (rewritten)
        {
            char* bracketed_from = add_brackets(rewritten);
            free(rewritten);
            if (smfi_chgfrom(ctx, bracketed_from,
                             NULL)
                != MI_SUCCESS)  // TODO maybe add ESMTP arguments?
            {
                free(bracketed_from);
                return SMFIS_TEMPFAIL;
            }
            free(bracketed_from);
        }
    }
    return SMFIS_CONTINUE;
}

static sfsistat on_close(SMFICTX* ctx)
{
    privdata_t* priv = smfi_getpriv(ctx);
    smfi_setpriv(ctx, NULL);
    free(priv->envfrom);
    list_destroy(priv->envrcpt, free);
    free(priv);
    return SMFIS_CONTINUE;
}

/* clang-format off */
static struct smfiDesc smfilter = {
    "PostSRSd", SMFI_VERSION, SMFIF_CHGFROM | SMFIF_ADDRCPT | SMFIF_DELRCPT,
    on_connect,
    NULL /* helo */,
    on_envfrom,
    on_envrcpt,
    NULL /* header */,
    NULL /* eoh */,
    NULL /* body */,
    on_eom,
    NULL /* abort */,
    on_close,
    NULL /* unknown */,
    NULL /* data */,
    NULL /* negotiate */,
};
/* clang-format on */
#endif

void milter_main(cfg_t* cfg, srs_t* srs, const char* srs_domain,
                 domain_set_t* local_domains)
{
    MAYBE_UNUSED(cfg);
    MAYBE_UNUSED(srs);
    MAYBE_UNUSED(srs_domain);
    MAYBE_UNUSED(local_domains);
#ifdef WITH_MILTER
    if (strncasecmp(milter_uri, "unix:", 5) == 0)
        milter_path = milter_uri + 5;
    else if (strncasecmp(milter_uri, "local:", 6) == 0)
        milter_path = milter_uri + 6;
    if (milter_path)
        milter_lock = acquire_lock(milter_path);
    if (milter_lock > 0)
        unlink(milter_path);
    if (smfi_setconn(milter_uri) == MI_FAILURE)
    {
        log_error("cannot start milter: smfi_setconn failed");
        goto done;
    }
    if (smfi_register(smfilter) == MI_FAILURE)
    {
        log_error("cannot start milter: failed to register callbacks");
        goto done;
    }
    g_cfg = cfg;
    g_srs = srs;
    g_srs_domain = srs_domain;
    g_local_domains = local_domains;
    smfi_main(NULL);
done:
    if (milter_path && milter_lock > 0)
    {
        release_lock(milter_path, milter_lock);
    }
#endif
}
