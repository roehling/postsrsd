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

#include "postsrsd_build_config.h"
#include "util.h"

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
    log_error("no milter support");
    return false;
#endif
}

#ifdef WITH_MILTER
static sfsistat on_connect(SMFICTX* ctx, char*, _SOCK_ADDR*)
{
    return SMFIS_CONTINUE;
}

static sfsistat on_envfrom(SMFICTX* ctx, char**)
{
    return SMFIS_CONTINUE;
}

static sfsistat on_envrcpt(SMFICTX* ctx, char**)
{
    return SMFIS_CONTINUE;
}

static sfsistat on_eom(SMFICTX* ctx)
{
    return SMFIS_CONTINUE;
}

static sfsistat on_close(SMFICTX* ctx)
{
    return SMFIS_CONTINUE;
}

static struct smfiDesc smfilter = {
    "PostSRSd", SMFI_VERSION, SMFIF_CHGFROM | SMFIF_ADDRCPT | SMFIF_DELRCPT,
    on_connect, NULL,               /* helo */
    on_envfrom, on_envrcpt,   NULL, /* header */
    NULL,                           /* eoh */
    NULL,                           /* body */
    on_eom,     NULL,               /* abort */
    on_close,   NULL,               /* unknown */
    NULL,                           /* data */
    NULL,                           /* negotiate */
};
#endif

void milter_main(cfg_t* cfg, srs_t* srs, const char* srs_domain,
                 domain_set_t* local_domains)
{
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