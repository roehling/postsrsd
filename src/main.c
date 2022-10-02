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
#include "database.h"
#include "endpoint.h"
#include "milter.h"
#include "postsrsd_build_config.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_ERRNO_H
#    include <errno.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#    include <sys/socket.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#    include <sys/types.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#    include <sys/wait.h>
#endif
#ifdef HAVE_WAIT_H
#    include <wait.h>
#endif
#ifdef HAVE_SIGNAL_H
#    include <signal.h>
#endif
#ifdef HAVE_PWD_H
#    include <pwd.h>
#endif
#ifdef HAVE_POLL_H
#    include <poll.h>
#endif
#ifdef HAVE_UNISTD_H
#    include <unistd.h>
#endif

static bool drop_privileges(cfg_t* cfg)
{
    int target_uid = 0;
    int target_gid = 0;
    const char* user = cfg_getstr(cfg, "user");
    const char* chroot_dir = cfg_getstr(cfg, "chroot-dir");
    if (user && *user)
    {
#ifdef HAVE_PWD_H
        struct passwd* pwd = NULL;
        pwd = getpwnam(user);
        if (pwd == NULL)
        {
            log_error("cannot drop privileges: no such user: %s", user);
            return false;
        }
        target_uid = pwd->pw_uid;
        target_gid = pwd->pw_gid;
#else
        log_error("cannot drop privileges: not supported by system");
        return false;
#endif
    }
    if (chroot_dir && *chroot_dir)
    {
        if (chroot(chroot_dir) < 0)
        {
            log_perror(errno, "cannot drop privileges: chroot");
            return false;
        }
    }
    if (target_uid != 0 || target_gid != 0)
    {
        if (setgid(target_gid) < 0)
        {
            log_perror(errno, "cannot drop privileges: setgid");
            return false;
        }
        if (setuid(target_uid) < 0)
        {
            log_perror(errno, "cannot drop privileges: setuid");
        }
    }
    return false;
}

static bool prepare_database(cfg_t* cfg)
{
    if (cfg_getint(cfg, "original-envelope") == SRS_ENVELOPE_DATABASE)
    {
        database_t* db =
            database_connect(cfg_getstr(cfg, "envelope-database"), true);
        if (!db)
            return false;
        database_expire(db);
        database_disconnect(db);
    }
    return true;
}

static bool daemonize(cfg_t* cfg)
{
    if (!cfg_getbool(cfg, "daemonize"))
        return true;
    close(0);
    close(1);
    close(2);
    if (fork() != 0)
        exit(EXIT_SUCCESS);
    setsid();
    if (fork() != 0)
        exit(EXIT_SUCCESS);
    return true;
}

static void handle_socketmap_client(cfg_t* cfg, srs_t* srs, int conn)
{
    FILE* fp_read = fdopen(conn, "r");
    if (fp_read == NULL)
        return;
    FILE* fp_write = fdopen(dup(conn), "w");
    if (fp_write == NULL)
        return;
    database_t* db = NULL;
    if (cfg_getint(cfg, "original-envelope") == SRS_ENVELOPE_DATABASE)
    {
        db = database_connect(cfg_getstr(cfg, "envelope-database"), false);
        if (!conn)
            return;
    }
    (void)srs;
    (void)db;
}

int main(int argc, char** argv)
{
    cfg_t* cfg = NULL;
    srs_t* srs = NULL;
    struct domain_set* local_domains = NULL;
    char* srs_domain = NULL;
    FILE* pf = NULL;
    int socketmaps[4] = {-1, -1, -1, -1};
    int num_sockets = 0;
    int milter_pid = 0;
    int exit_code = EXIT_FAILURE;
#ifdef HAVE_CLOSE_RANGE
    close_range(3, ~0U, 0);
#else
    for (int fd = 3; fd < 1024; ++fd)
        close(fd);
#endif
    cfg = config_from_commandline(argc, argv);
    if (!cfg)
        goto shutdown;
    srs = srs_from_config(cfg);
    if (!srs)
        goto shutdown;
    if (!srs_domains_from_config(cfg, &srs_domain, &local_domains))
        goto shutdown;
    const char* socketmap_endpoint = cfg_getstr(cfg, "socketmap");
    if (socketmap_endpoint && *socketmap_endpoint)
    {
        num_sockets = endpoint_create(
            socketmap_endpoint, sizeof(socketmaps) / sizeof(int), socketmaps);
        if (num_sockets < 0)
            goto shutdown;
    }
    const char* milter_endpoint = cfg_getstr(cfg, "milter");
    if (milter_endpoint && *milter_endpoint)
    {
        if (!milter_create(milter_endpoint))
            goto shutdown;
    }
    else
    {
        milter_endpoint = NULL;
    }
    const char* pid_file = cfg_getstr(cfg, "pid-file");
    if (pid_file && *pid_file)
    {
        pf = fopen(pid_file, "w");
        if (!pf)
        {
            log_error("cannot open %s for writing", pid_file);
            goto shutdown;
        }
    }
    if (!drop_privileges(cfg))
        goto shutdown;
    if (!prepare_database(cfg))
        goto shutdown;
    if (!daemonize(cfg))
        goto shutdown;
    if (pf)
    {
        fprintf(pf, "%d", (int)getpid());
        fclose(pf);
        pf = NULL;
    }
    exit_code = EXIT_SUCCESS;
    if (num_sockets > 0)
    {
        if (milter_endpoint && *milter_endpoint)
        {
            milter_pid = fork();
            if (milter_pid == 0)
            {
                for (unsigned i = 0; i < sizeof(socketmaps) / sizeof(int); ++i)
                {
                    if (socketmaps[i] >= 0)
                        close(socketmaps[i]);
                    socketmaps[i] = -1;
                }
                milter_main();
                goto shutdown;
            }
        }
        struct pollfd fds[sizeof(socketmaps) / sizeof(int)];
        for (unsigned i = 0; i < (unsigned)num_sockets; ++i)
        {
            fds[i].fd = socketmaps[i];
            fds[i].events = POLLIN;
        }
        for (;;)
        {
            if (poll(fds, num_sockets, 1000) < 0)
            {
                if (errno == EINTR)
                    continue;
                log_perror(errno, "poll");
                goto shutdown;
            }
            for (unsigned i = 0; i < (unsigned)num_sockets; ++i)
            {
                if (fds[i].revents)
                {
                    int conn = accept(fds[i].fd, NULL, NULL);
                    if (conn < 0)
                        continue;
                    if (fork() == 0)
                    {
                        for (unsigned j = 0; j < (unsigned)num_sockets; ++j)
                            close(socketmaps[j]);
                        handle_socketmap_client(cfg, srs, conn);
                        exit(EXIT_SUCCESS);
                    }
                }
            }
            waitpid(-1, NULL, WNOHANG);
        }
    }
    else if (milter_endpoint && *milter_endpoint)
    {
        milter_main();
    }
shutdown:
    for (unsigned i = 0; i < sizeof(socketmaps) / sizeof(int); ++i)
        if (socketmaps[i] >= 0)
            close(socketmaps[i]);
    if (pf)
        fclose(pf);
    free(srs_domain);
    if (local_domains)
        domain_set_destroy(local_domains);
    if (srs)
        srs_free(srs);
    if (cfg)
        cfg_free(cfg);
    if (milter_pid > 0)
    {
        kill(milter_pid, SIGTERM);
        waitpid(milter_pid, NULL, 0);
    }
    return exit_code;
}
