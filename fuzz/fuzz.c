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

#include "fuzz.h"

postsrsd_t* fuzz_state_create()
{
    postsrsd_t* state = malloc(sizeof(postsrsd_t));
    if (state == NULL)
        return NULL;
    init_state(state);

    state->cfg = config_defaults();
    if (state->cfg == NULL)
        goto fail;
    cfg_setstr(state->cfg, "chroot-dir", "");
    cfg_setstr(state->cfg, "unprivileged-user", "");
    cfg_setbool(state->cfg, "seccomp", cfg_false);
    cfg_setint(state->cfg, "keep-alive", 1);
    cfg_setint(state->cfg, "milter-recipient-limit", 1000);
    cfg_setbool(state->cfg, "always-rewrite", cfg_true);

    state->srs = srs_new();
    if (state->srs == NULL)
        goto fail;
    srs_add_secret(state->srs, "tops3cr3t");

    state->srs_domain = strdup("example.com");
    if (state->srs_domain == NULL)
        goto fail;

    state->local_domains = domain_set_create();
    if (state->local_domains == NULL)
        goto fail;
    domain_set_add(state->local_domains, "example.com");
    return state;

fail:
    finalize_state(state);
    free(state);
    return NULL;
}

void fuzz_state_destroy(postsrsd_t* state)
{
    finalize_state(state);
    free(state);
}
