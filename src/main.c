/* PostSRSd - Sender Rewriting Scheme daemon for Postfix
 * Copyright 2012-2023 Timo Röhling <timo@gaussglocke.de>
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
#include "netstring.h"
#include "postsrsd_build_config.h"
#include "srs.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_FCNTL_H
#    include <fcntl.h>
#endif
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
#ifdef HAVE_GRP_H
#    include <grp.h>
#endif

static volatile sig_atomic_t timeout = 0;
static volatile sig_atomic_t sighup_received = 0, sigterm_received = 0;

static bool prepare_unprivileged_work(cfg_t* cfg, int* target_uid,
                                      int* target_gid)
{
    if (target_uid == NULL || target_gid == NULL)
        return false;
    *target_uid = 0;
    *target_gid = 0;
    const char* chroot_dir = cfg_getstr(cfg, "chroot-dir");
    const char* user = cfg_getstr(cfg, "unprivileged-user");
    if (NONEMPTY_STRING(user))
    {
#ifdef HAVE_PWD_H
        struct passwd* pwd = NULL;
        pwd = getpwnam(user);
        if (pwd == NULL)
        {
            log_error("cannot drop privileges: no such user: %s", user);
            return false;
        }
        *target_uid = pwd->pw_uid;
        *target_gid = pwd->pw_gid;
        if (chdir(pwd->pw_dir) < 0 && NULL_OR_EMPTY_STRING(chroot_dir))
        {
            log_warn("cannot chdir to home directory of user %s: %s", user,
                     strerror(errno));
        }
#else
        log_error("cannot drop privileges: not supported by system");
        return false;
#endif
    }
    if (NONEMPTY_STRING(chroot_dir))
    {
#ifdef HAVE_CHROOT
        if (chdir(chroot_dir) < 0)
        {
            log_perror(errno,
                       "cannot drop privileges: failed to chdir to chroot");
            return false;
        }
#else
        log_error("chroot is not supported on this system");
        return false;
#endif
    }
    return true;
}

static bool drop_privileges(cfg_t* cfg, int target_uid, int target_gid)
{
    const char* chroot_dir = cfg_getstr(cfg, "chroot-dir");
    if (NONEMPTY_STRING(chroot_dir))
    {
#ifdef HAVE_CHROOT
        if (chdir(chroot_dir) < 0)
        {
            log_perror(errno,
                       "cannot drop privileges: failed to chdir to chroot");
            return false;
        }
        if (chroot(chroot_dir) < 0)
        {
            log_perror(errno, "cannot drop privileges: chroot");
            return false;
        }
#else
        log_error("chroot is not supported on this system");
        return false;
#endif
    }
    if (target_uid != 0 || target_gid != 0)
    {
#ifdef HAVE_SETGROUPS
        if (setgroups(0, NULL) < 0)
        {
            log_perror(errno, "cannot drop privileges: setgroups");
            return false;
        }
#endif
        if (setgid(target_gid) < 0)
        {
            log_perror(errno, "cannot drop privileges: setgid");
            return false;
        }
        if (setuid(target_uid) < 0)
        {
            log_perror(errno, "cannot drop privileges: setuid");
            return false;
        }
    }
    return true;
}

static bool check_unprivileged_work(cfg_t* cfg, int target_uid, int target_gid)
{
    int status;
    pid_t worker_pid = fork();
    if (worker_pid < 0)
    {
        log_perror(errno, "fork");
        return false;
    }
    if (worker_pid == 0)
    {
        if (!drop_privileges(cfg, target_uid, target_gid))
            exit(EXIT_FAILURE);
        if (cfg_getint(cfg, "original-envelope") == SRS_ENVELOPE_DATABASE)
        {
            database_t* db =
                database_connect(cfg_getstr(cfg, "envelope-database"), true);
            if (db == NULL)
                exit(EXIT_FAILURE);
            database_expire(db);
            database_disconnect(db);
        }
        exit(EXIT_SUCCESS);
    }
    if (waitpid(worker_pid, &status, 0) < 0)
    {
        log_perror(errno, "waitpid");
        return false;
    }
    return (WEXITSTATUS(status) == EXIT_SUCCESS);
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

static void on_sigalrm(int signum)
{
    timeout = signum;
}

static void on_sighup(int signum)
{
    sighup_received = signum;
}

static void on_sigterm(int signum)
{
    sigterm_received = signum;
}

static void handle_socketmap_client(cfg_t* cfg, srs_t* srs,
                                    const char* srs_domain,
                                    domain_set_t* local_domains, int conn)
{
#ifdef HAVE_FCNTL_H
    int flags = fcntl(conn, F_GETFL);
    if (flags & O_NONBLOCK)
    {
        if (fcntl(conn, F_SETFL, flags & ~O_NONBLOCK) < 0)
        {
            log_error("failed to make socket connection blocking");
            return;
        }
    }
#endif
    FILE* fp_write = fdopen(dup(conn), "w");
    if (fp_write == NULL)
        return;
    FILE* fp_read = fdopen(conn, "r");
    if (fp_read == NULL)
        return;
    database_t* db = NULL;
    if (cfg_getint(cfg, "original-envelope") == SRS_ENVELOPE_DATABASE)
    {
        db = database_connect(cfg_getstr(cfg, "envelope-database"), false);
        if (db == NULL)
            return;
    }
    signal(SIGALRM, on_sigalrm);
    signal(SIGUSR1, on_sighup);
    signal(SIGTERM, SIG_DFL);
    int keep_alive = cfg_getint(cfg, "keep-alive");
    for (;;)
    {
        char buffer[1024];
        size_t len;
        char* addr;
        bool error;
        if (sighup_received)
            break;
        timeout = 0;
        alarm(keep_alive);
        char* request = netstring_read(fp_read, buffer, sizeof(buffer), &len);
        if (timeout)
            break;
        if (request == NULL)
        {
            if (!feof(fp_read) && !ferror(fp_read))
            {
                netstring_write(fp_write, "PERM Invalid query.", 19);
                fflush(fp_write);
                log_error("invalid socketmap query, closing connection");
            }
            break;
        }
        alarm(0);
        char* query_type = strtok_r(request, " ", &addr);
        if (query_type == NULL)
        {
            netstring_write(fp_write, "PERM Invalid query.", 19);
            fflush(fp_write);
            log_error("invalid socketmap query, closing connection");
            break;
        }
        if (len > 512 + (size_t)(addr - request))
        {
            netstring_write(fp_write, "PERM Too big.", 13);
            fflush(fp_write);
            log_warn("socketmap query is too big");
            continue;
        }
        char* rewritten = NULL;
        const char* info = NULL;
        if (strcmp(query_type, "forward") == 0)
        {
            rewritten = postsrsd_forward(addr, srs_domain, srs, db,
                                         local_domains, &error, &info);
        }
        else if (strcmp(query_type, "reverse") == 0)
        {
            rewritten = postsrsd_reverse(addr, srs, db, &error, &info);
        }
        else
        {
            error = true;
            info = "Invalid map.";
            log_warn("invalid key in socketmap query");
        }
        if (rewritten)
        {
            strcpy(buffer, "OK ");
            strncat(buffer, rewritten, sizeof(buffer) - 4);
            free(rewritten);
            netstring_write(fp_write, buffer, strlen(buffer));
        }
        else
        {
            if (error)
            {
                strcpy(buffer, "PERM ");
            }
            else
            {
                strcpy(buffer, "NOTFOUND ");
            }
            if (info)
                strncat(buffer, info, sizeof(buffer) - 10);
            netstring_write(fp_write, buffer, strlen(buffer));
        }
        fflush(fp_write);
    }
    database_disconnect(db);
}

bool reload_srs_configuration(int argc, char** argv, cfg_t** cfg, srs_t** srs,
                              char** srs_domain, domain_set_t** local_domains)
{
    set_string(srs_domain, NULL);
    if (*local_domains != NULL)
    {
        domain_set_destroy(*local_domains);
        *local_domains = NULL;
    }
    if (*srs != NULL)
    {
        srs_free(*srs);
        *srs = NULL;
    }
    if (*cfg != NULL)
    {
        cfg_free(*cfg);
        *cfg = NULL;
    }
    kill(0, SIGUSR1);
    *cfg = config_from_commandline(argc, argv);
    if (*cfg == NULL)
        return false;
    *srs = srs_from_config(*cfg);
    if (*srs == NULL)
        return false;
    if (!srs_domains_from_config(*cfg, srs_domain, local_domains))
        return false;
    return true;
}

int main(int argc, char** argv)
{
    cfg_t* cfg = NULL;
    srs_t* srs = NULL;
    domain_set_t* local_domains = NULL;
    char* srs_domain = NULL;
    FILE* pf = NULL;
    int socketmaps[4] = {-1, -1, -1, -1};
    int num_sockets = 0;
    int milter_pid = 0;
    int exit_code = EXIT_FAILURE;
    int target_uid = 0, target_gid = 0;
#ifdef HAVE_CLOSE_RANGE
    close_range(3, ~0U, 0);
#else
    for (int fd = 3; fd < 1024; ++fd)
        close(fd);
#endif
    cfg = config_from_commandline(argc, argv);
    if (cfg == NULL)
        goto shutdown;
    if (cfg_getbool(cfg, "syslog"))
        log_enable_syslog();
    if (cfg_getbool(cfg, "debug"))
        log_set_verbosity(LogDebug);
    if (!unprivileged_user_from_config(cfg, &target_uid, &target_gid))
        goto shutdown;
    srs = srs_from_config(cfg);
    if (srs == NULL)
        goto shutdown;
    if (!srs_domains_from_config(cfg, &srs_domain, &local_domains))
        goto shutdown;
    const char* socketmap_endpoint = cfg_getstr(cfg, "socketmap");
    if (NONEMPTY_STRING(socketmap_endpoint))
    {
        num_sockets = endpoint_create(
            socketmap_endpoint, sizeof(socketmaps) / sizeof(int), socketmaps);
        if (num_sockets < 0)
            goto shutdown;
    }
    const char* milter_endpoint = cfg_getstr(cfg, "milter");
    if (NONEMPTY_STRING(milter_endpoint))
    {
        if (!milter_create(milter_endpoint))
            goto shutdown;
    }
    else
    {
        milter_endpoint = NULL;
    }
    const char* pid_file = cfg_getstr(cfg, "pid-file");
    if (NONEMPTY_STRING(pid_file))
    {
        pf = fopen(pid_file, "w");
        if (pf == NULL)
        {
            log_error("cannot open %s for writing", pid_file);
            goto shutdown;
        }
    }
    if (!check_unprivileged_work(cfg, target_uid, target_gid))
        goto shutdown;
    if (!daemonize(cfg))
        goto shutdown;
    if (pf != NULL)
    {
        fprintf(pf, "%d", (int)getpid());
        fclose(pf);
        pf = NULL;
    }
    exit_code = EXIT_SUCCESS;
    if (num_sockets > 0)
    {
        if (NONEMPTY_STRING(milter_endpoint))
        {
            milter_pid = fork();
            if (milter_pid == 0)
            {
                if (drop_privileges(cfg, target_uid, target_gid))
                {
                    for (unsigned i = 0; i < sizeof(socketmaps) / sizeof(int);
                         ++i)
                    {
                        if (socketmaps[i] >= 0)
                            close(socketmaps[i]);
                        socketmaps[i] = -1;
                    }
                    milter_main(cfg, srs, srs_domain, local_domains);
                }
                goto shutdown;
            }
        }
        signal(SIGALRM, SIG_IGN);
        signal(SIGUSR1, SIG_IGN);
        signal(SIGHUP, on_sighup);
        signal(SIGTERM, on_sigterm);
        struct pollfd fds[sizeof(socketmaps) / sizeof(int)];
        unsigned num_fds = 0;
        for (unsigned i = 0; i < (unsigned)num_sockets; ++i)
        {
            fds[num_fds].fd = socketmaps[i];
            fds[num_fds].events = POLLIN;
            ++num_fds;
        }
        for (;;)
        {
            if (sighup_received)
            {
                sighup_received = 0;
                log_info("SIGHUP received. Reloading SRS configuration.");
                if (!reload_srs_configuration(argc, argv, &cfg, &srs,
                                              &srs_domain, &local_domains))
                    goto shutdown;
            }
            if (sigterm_received)
            {
                sigterm_received = 0;
                log_info("SIGTERM received. shutting down.");
                goto shutdown;
            }
            if (poll(fds, num_fds, 1000) < 0)
            {
                if (errno == EINTR)
                    continue;
                log_perror(errno, "poll");
                goto shutdown;
            }
            for (unsigned i = 0; i < num_fds; ++i)
            {
                if (fds[i].revents)
                {
                    int conn = accept(fds[i].fd, NULL, NULL);
                    if (conn < 0)
                    {
                        log_perror(errno, "accept");
                        continue;
                    }
                    pid_t pid = fork();
                    if (pid == 0)
                    {
                        handle_socketmap_client(cfg, srs, srs_domain,
                                                local_domains, conn);
                        exit(EXIT_SUCCESS);
                    }
                    if (pid < 0)
                    {
                        log_perror(errno, "fork");
                    }
                    close(conn);
                }
            }
            waitpid(0, NULL, WNOHANG);
        }
    }
    else if (NONEMPTY_STRING(milter_endpoint))
    {
        milter_main(cfg, srs, srs_domain, local_domains);
    }
shutdown:
    for (unsigned i = 0; i < sizeof(socketmaps) / sizeof(int); ++i)
        if (socketmaps[i] >= 0)
            close(socketmaps[i]);
    if (pf != NULL)
        fclose(pf);
    set_string(&srs_domain, NULL);
    if (local_domains != NULL)
    {
        domain_set_destroy(local_domains);
        local_domains = NULL;
    }
    if (srs != NULL)
    {
        srs_free(srs);
        srs = NULL;
    }
    if (cfg != NULL)
    {
        cfg_free(cfg);
        cfg = NULL;
    }
    kill(0, SIGUSR1);
    if (milter_pid > 0)
    {
        kill(milter_pid, SIGTERM);
        waitpid(milter_pid, NULL, 0);
    }
    return exit_code;
}
