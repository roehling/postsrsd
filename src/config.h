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
#ifndef CONFIG_H
#define CONFIG_H

#define SRS_ENVELOPE_EMBEDDED 0
#define SRS_ENVELOPE_DATABASE 1

#include "srs2.h"
#include "util.h"

#include <confuse.h>

cfg_t* config_from_commandline(int argc, char* const* argv);
srs_t* srs_from_config(cfg_t* cfg);
bool srs_domains_from_config(cfg_t* cfg, char** srs_domain,
                             struct domain_set** other_domains);
#endif
