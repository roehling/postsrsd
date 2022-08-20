/* PostSRSd - Sender Rewriting Scheme daemon for Postfix
 * Copyright (c) 2012-2022 Timo Röhling <timo@gaussglocke.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
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

#include <confuse.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_SYS_TYPES_H
#    include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
#    include <sys/stat.h>
#endif
#include <unistd.h>

static void set_string(char** var, char* value)
{
    free(*var);
    *var = value;
}

static void free_and_nullify(char** var)
{
    free(*var);
    *var = NULL;
}

static int file_exists(const char* filename)
{
    struct stat st;
    if (stat(filename, &st) < 0)
        return 0;
    return S_ISREG(st.st_mode);
}

void config_create(struct config* cfg)
{
    cfg->socket_forward_endpoint = strdup("unix:/var/spool/postfix/srs_forward");
    cfg->socket_reverse_endpoint = strdup("unix:/var/spool/postfix/srs_reverse");
    cfg->milter_endpoint = strdup("unix:/var/spool/postfix/srs_milter");
    cfg->pid_file = NULL;
    cfg->daemonize = 0;
}

void config_destroy(struct config* cfg)
{
    free_and_nullify(&cfg->socket_forward_endpoint);
    free_and_nullify(&cfg->socket_reverse_endpoint);
    free_and_nullify(&cfg->milter_endpoint);
    free_and_nullify(&cfg->pid_file);
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
    return 1;
}

int config_load(struct config* cfg, const char* filename)
{
    return 0;
}
