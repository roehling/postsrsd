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

static bool has_uri = false;

bool milter_create(const char* uri)
{
#ifdef WITH_MILTER
    (void)uri;
    has_uri = true;
    return true;
#else
    (void)uri;
    log_error("no milter support");
    return false;
#endif
}

void milter_main()
{
#ifdef WITH_MILTER
#endif
}