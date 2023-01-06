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
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifndef HAVE_STRCASECMP
#    ifdef HAVE__STRICMP
#        define strcasecmp _stricmp
#    endif
#endif

static int parse_original_envelope(cfg_t* cfg, cfg_opt_t* opt,
                                   const char* value, void* result)
{
    if (strcasecmp(value, "embedded") == 0)
        *(int*)result = SRS_ENVELOPE_EMBEDDED;
    else if (strcasecmp(value, "database") == 0)
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

static int validate_uint(cfg_t* cfg, cfg_opt_t* opt)
{
    int value = cfg_opt_getnint(opt, cfg_opt_size(opt) - 1);
    if (value < 0)
    {
        cfg_error(cfg, "option '%s' must be non-negative", cfg_opt_name(opt));
        return -1;
    }
    return 0;
}

static bool is_valid_domain_name(const char* s)
{
    char prev = 0;
    if (!s)
        return false;
    if (!*s)
        return false;
    while (*s)
    {
        if (*s == '.' && prev == '.')
            return false;
        if (!isalnum(*s) && *s != '-' && *s != '.')
            return false;
        prev = *s++;
    }
    return prev != '.';
}

static int validate_domain_names(cfg_t* cfg, cfg_opt_t* opt)
{
    unsigned ndomains = cfg_opt_size(opt);
    for (unsigned i = 0; i < ndomains; ++i)
    {
        const char* domain = cfg_opt_getnstr(opt, i);
        if (!is_valid_domain_name(domain))
        {
            cfg_error(cfg, "option '%s' has invalid domain name '%s'",
                      cfg_opt_name(opt), domain);
            return -1;
        }
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
        CFG_STR("envelope-database", NULL, CFGF_NODEFAULT),
        CFG_STR("pid-file", NULL, CFGF_NODEFAULT),
        CFG_STR("unprivileged-user", DEFAULT_POSTSRSD_USER, CFGF_NONE),
        CFG_STR("chroot-dir", DEFAULT_CHROOT_DIR, CFGF_NONE),
        CFG_BOOL("daemonize", cfg_false, CFGF_NONE),
        CFG_BOOL("syslog", cfg_false, CFGF_NONE),
        CFG_END(),
    };
    cfg_t* cfg = cfg_init(opts, CFGF_NONE);
    cfg_set_validate_func(cfg, "separator", validate_separator);
    cfg_set_validate_func(cfg, "srs-domain", validate_domain_names);
    cfg_set_validate_func(cfg, "domains", validate_domain_names);
    cfg_set_validate_func(cfg, "keep-alive", validate_uint);
    int opt;
    char* config_file = NULL;
    char* pid_file = NULL;
    char* chroot_dir = NULL;
    char* unprivileged_user = NULL;
    int daemonize = 0;
    int ok = 1;
    if (file_exists(DEFAULT_CONFIG_FILE))
        set_string(&config_file, strdup(DEFAULT_CONFIG_FILE));
    while ((opt = getopt(argc, argv, "C:c:Dp:u:v")) != -1)
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
            case 'v':
                puts(POSTSRSD_VERSION);
                exit(0);
                break;
            default:
                break;
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
                break;
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

srs_t* srs_from_config(cfg_t* cfg)
{
    srs_t* srs = srs_new();
    srs_set_alwaysrewrite(srs, cfg_getbool(cfg, "always-rewrite"));
    srs_set_hashlength(srs, cfg_getint(cfg, "hash-length"));
    srs_set_hashmin(srs, cfg_getint(cfg, "hash-minimum"));
    srs_set_separator(srs, cfg_getstr(cfg, "separator")[0]);
    char* secrets_file = cfg_getstr(cfg, "secrets-file");
    if (secrets_file && secrets_file[0])
    {
        FILE* f = fopen(secrets_file, "r");
        if (f)
        {
            char buffer[1024];
            char* secret;
            while ((secret = fgets(buffer, sizeof(buffer), f)) != NULL)
            {
                secret = strtok(secret, "\r\n");
                if (secret && secret[0])
                    srs_add_secret(srs, secret);
            }
            fclose(f);
        }
        else
        {
            log_error("cannot read secrets from %s", secrets_file);
            srs_free(srs);
            return NULL;
        }
    }
    return srs;
}

bool srs_domains_from_config(cfg_t* cfg, char** srs_domain,
                             struct domain_set** local_domains)
{
    *srs_domain = NULL;
    *local_domains = domain_set_create();
    char* domain;
    domain = cfg_getstr(cfg, "srs-domain");
    if (domain && domain[0])
        *srs_domain = strdup(domain[0] == '.' ? domain + 1 : domain);
    unsigned ndomains = cfg_size(cfg, "domains");
    for (unsigned i = 0; i < ndomains; ++i)
    {
        domain = cfg_getnstr(cfg, "domains", i);
        if (domain && domain[0])
        {
            domain_set_add(*local_domains, domain);
            if (*srs_domain == NULL)
                *srs_domain = strdup(domain[0] == '.' ? domain + 1 : domain);
        }
    }
    char* domains_file = cfg_getstr(cfg, "domains-file");
    if (domains_file && domains_file[0])
    {
        FILE* f = fopen(domains_file, "r");
        if (f)
        {
            char buffer[1024];
            while ((domain = fgets(buffer, sizeof(buffer), f)) != NULL)
            {
                domain = strtok(domain, "\r\n");
                if (domain && domain[0])
                {
                    if (is_valid_domain_name(domain))
                    {
                        domain_set_add(*local_domains, domain);
                        if (*srs_domain == NULL)
                            *srs_domain =
                                strdup(domain[0] == '.' ? domain + 1 : domain);
                    }
                    else
                    {
                        log_error("invalid domain name '%s' in domains file",
                                  domain);
                        goto fail;
                    }
                }
            }
            fclose(f);
        }
        else
        {
            log_error("cannot read local domains from %s", domains_file);
            goto fail;
        }
    }
    return true;
fail:
    domain_set_destroy(*local_domains);
    *local_domains = NULL;
    free(*srs_domain);
    *srs_domain = NULL;
    return false;
}
