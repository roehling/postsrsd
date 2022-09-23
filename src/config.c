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

#include <errno.h>
#include <string.h>
#include <unistd.h>

static int parse_original_envelope(cfg_t* cfg, cfg_opt_t* opt,
                                   const char* value, void* result)
{
    if (strcasecmp(value, "embedded"))
        *(int*)result = SRS_ENVELOPE_EMBEDDED;
    else if (strcasecmp(value, "database"))
        *(int*)result = SRS_ENVELOPE_DATABASE;
    else
    {
        cfg_error(cfg, "option '%s' must be either 'embedded' or 'database'",
                  cfg_opt_name(opt));
        return -1;
    }
    return 0;
}

static int validate_separator(cfg_t* cfg, cfg_opt_t* opt)
{
    const char* value = cfg_opt_getstr(opt);
    if (strlen(value) != 1 || !strpbrk(value, "=+-"))
    {
        cfg_error(cfg, "option '%s' must be one of '=', '+', '-'",
                  cfg_opt_name(opt));
        return -1;
    }
    return 0;
}

cfg_t* config_from_commandline(int argc, char* const* argv)
{
    static cfg_opt_t opts[] = {
        CFG_STR("srs-domain", NULL, CFGF_NODEFAULT),
        CFG_STR_LIST("domains", "{}", CFGF_NONE),
        CFG_STR("domains-file", NULL, CFGF_NODEFAULT),
        CFG_INT_CB("original-envelope", SRS_ENVELOPE_EMBEDDED, CFGF_NONE,
                   parse_original_envelope),
        CFG_STR("separator", "=", CFGF_NONE),
        CFG_INT("hash-length", 4, CFGF_NONE),
        CFG_INT("hash-minimum", 4, CFGF_NONE),
        CFG_BOOL("always-rewrite", cfg_false, CFGF_NONE),
        CFG_STR("socketmap", "unix:/var/spool/postfix/srs", CFGF_NONE),
        CFG_INT("keep-alive", 30, CFGF_NONE),
        CFG_STR("milter", NULL, CFGF_NODEFAULT),
        CFG_STR("secrets-file", DEFAULT_SECRETS_FILE, CFGF_NONE),
        CFG_STR("envelope-database", DEFAULT_ENVELOPE_DATABASE, CFGF_NONE),
        CFG_STR("pid-file", NULL, CFGF_NODEFAULT),
        CFG_STR("unprivileged-user", DEFAULT_POSTSRSD_USER, CFGF_NONE),
        CFG_STR("chroot-dir", DEFAULT_CHROOT_DIR, CFGF_NONE),
        CFG_BOOL("daemonize", cfg_false, CFGF_NONE),
        CFG_END(),
    };
    cfg_t* cfg = cfg_init(opts, CFGF_NONE);
    cfg_set_validate_func(cfg, "separator", validate_separator);
    int opt;
    char* config_file = NULL;
    char* pid_file = NULL;
    char* chroot_dir = NULL;
    char* unprivileged_user = NULL;
    int daemonize = 0;
    int ok = 1;
    if (file_exists(DEFAULT_CONFIG_FILE))
        set_string(&config_file, strdup(DEFAULT_CONFIG_FILE));
    while ((opt = getopt(argc, argv, "C:c:Dp:u:")) != -1)
    {
        switch (opt)
        {
            case '?':
                return 0;
            case 'C':
                set_string(&config_file, strdup(optarg));
                break;
            case 'c':
                set_string(&chroot_dir, strdup(optarg));
                break;
            case 'D':
                daemonize = 1;
                break;
            case 'p':
                set_string(&pid_file, strdup(optarg));
                break;
            case 'u':
                set_string(&unprivileged_user, strdup(optarg));
                break;
            default:
        }
    }
    if (config_file)
    {
        switch (cfg_parse(cfg, config_file))
        {
            case CFG_FILE_ERROR:
                log_error("cannot read '%s': %s", config_file, strerror(errno));
                ok = 0;
                break;
            case CFG_PARSE_ERROR:
                log_error("malformed configuration file '%s'", config_file);
                ok = 0;
                break;
            default:
        }
        set_string(&config_file, NULL);
    }
    if (pid_file)
    {
        cfg_setstr(cfg, "pid-file", pid_file);
        set_string(&pid_file, NULL);
    }
    if (unprivileged_user)
    {
        cfg_setstr(cfg, "unprivileged-user", unprivileged_user);
        set_string(&unprivileged_user, NULL);
    }
    if (chroot_dir)
    {
        cfg_setstr(cfg, "chroot-dir", chroot_dir);
        set_string(&chroot_dir, NULL);
    }
    if (daemonize)
        cfg_setbool(cfg, "daemonize", cfg_true);
    if (ok)
        return cfg;
    cfg_free(cfg);
    return NULL;
}
