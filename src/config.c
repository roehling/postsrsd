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

#include "config.h"

#include "postsrsd_build_config.h"
#include "util.h"

#include <confuse.h>
#include <string.h>
#include <unistd.h>

void config_create(struct config* cfg)
{
    cfg->socketmap_endpoint = strdup("unix:/var/spool/postfix/srs");
    cfg->milter_endpoint = strdup("unix:/var/spool/postfix/srs_milter");
    cfg->pid_file = NULL;
    cfg->secrets_file = strdup(DEFAULT_SECRETS);
    cfg->tokens_db = strdup(DEFAULT_TOKENS_DB);
    cfg->daemonize = 0;
}

void config_destroy(struct config* cfg)
{
    set_string(&cfg->socketmap_endpoint, NULL);
    set_string(&cfg->milter_endpoint, NULL);
    set_string(&cfg->pid_file, NULL);
    set_string(&cfg->secrets_file, NULL);
    set_string(&cfg->tokens_db, NULL);
}

int config_parse_cmdline(struct config* cfg, int argc, char* const* argv)
{
    int opt;
    char* config_file = NULL;
    char* pid_file = NULL;
    int daemonize = 0;
    if (file_exists(DEFAULT_CONFIG))
        set_string(&config_file, strdup(DEFAULT_CONFIG));
    while ((opt = getopt(argc, argv, "c:p:D")) != -1)
    {
        switch (opt)
        {
            case '?':
                return 1;
            case 'c':
                set_string(&config_file, strdup(optarg));
                break;
            case 'p':
                set_string(&pid_file, strdup(optarg));
                break;
            case 'D':
                daemonize = 1;
                break;
        }
    }
    if (config_file)
    {
        if (!config_load(cfg, config_file))
            return 0;
    }
    if (pid_file)
        set_string(&cfg->pid_file, pid_file);
    if (daemonize)
        cfg->daemonize = 1;
    return 1;
}

int config_load(struct config* cfg, const char* filename)
{
    return 0;
}
