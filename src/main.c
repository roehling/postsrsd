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
static volatile sig_atomic_t sig_hup_received = 0, sig_term_received = 0;
static bool files_changed = false;

struct postsrsd
{
    cfg_t* cfg;
    srs_t* srs;
    endpoint_t* socketmap;
    char* srs_domain;
    domain_set_t* local_domains;
    file_watch_t* file_watch;
    int target_uid, target_gid;
};
typedef struct postsrsd postsrsd_t;

static void init_state(postsrsd_t* state)
{
    state->cfg = NULL;
    state->srs = NULL;
    state->socketmap = NULL;
    state->srs_domain = NULL;
    state->local_domains = NULL;
    state->file_watch = NULL;
    state->target_uid = 0;
    state->target_gid = 0;
}

static void finalize_state(postsrsd_t* state)
{
    if (state->file_watch != NULL)
    {
        file_watch_destroy(state->file_watch);
        state->file_watch = NULL;
    }
    if (state->socketmap != NULL)
    {
        endpoint_close(state->socketmap);
        state->socketmap = NULL;
    }
    set_string(&state->srs_domain, NULL);
    if (state->local_domains != NULL)
    {
        domain_set_destroy(state->local_domains);
        state->local_domains = NULL;
    }
    if (state->srs != NULL)
    {
        srs_free(state->srs);
        state->srs = NULL;
    }
    if (state->cfg != NULL)
    {
        cfg_free(state->cfg);
        state->cfg = NULL;
    }
}

static bool drop_privileges(postsrsd_t* state)
{
    const char* chroot_dir = cfg_getstr(state->cfg, "chroot-dir");
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
    if (state->target_uid != 0 || state->target_gid != 0)
    {
#ifdef HAVE_SETGROUPS
        if (setgroups(0, NULL) < 0)
        {
            log_perror(errno, "cannot drop privileges: setgroups");
            return false;
        }
#endif
        if (setgid(state->target_gid) < 0)
        {
            log_perror(errno, "cannot drop privileges: setgid");
            return false;
        }
        if (setuid(state->target_uid) < 0)
        {
            log_perror(errno, "cannot drop privileges: setuid");
            return false;
        }
    }
    return true;
}

static bool check_unprivileged_work(postsrsd_t* state)
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
        if (!drop_privileges(state))
            exit(EXIT_FAILURE);
        if (cfg_getint(state->cfg, "original-envelope")
            == SRS_ENVELOPE_DATABASE)
        {
            database_t* db = database_connect(
                cfg_getstr(state->cfg, "envelope-database"), true);
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

static bool daemonize(postsrsd_t* state)
{
    if (!cfg_getbool(state->cfg, "daemonize"))
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

static void on_sig_hup(int signum)
{
    sig_hup_received = signum;
}

static void on_sig_term(int signum)
{
    sig_term_received = signum;
}

static void handle_socketmap_client(postsrsd_t* state, int conn)
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
    if (cfg_getint(state->cfg, "original-envelope") == SRS_ENVELOPE_DATABASE)
    {
        db = database_connect(cfg_getstr(state->cfg, "envelope-database"),
                              false);
        if (db == NULL)
            return;
    }
    signal(SIGALRM, on_sigalrm);
    signal(SIGUSR1, on_sig_hup);
    signal(SIGTERM, SIG_DFL);
    int keep_alive = cfg_getint(state->cfg, "keep-alive");
    for (;;)
    {
        char buffer[1024];
        size_t len;
        char* addr;
        bool error;
        if (sig_hup_received)
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
            rewritten =
                postsrsd_forward(addr, state->srs_domain, state->srs, db,
                                 state->local_domains, &error, &info);
        }
        else if (strcmp(query_type, "reverse") == 0)
        {
            rewritten = postsrsd_reverse(addr, state->srs, db, &error, &info);
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

static void on_file_watch_event(const char* path, unsigned what, size_t cookie)
{
    MAYBE_UNUSED(path);
    MAYBE_UNUSED(cookie);
    if (what & FW_MODIFIED)
    {
        files_changed = true;
    }
}

static bool config_changed_str(cfg_t* old_cfg, cfg_t* new_cfg, const char* name)
{
    if (old_cfg == NULL)
        return new_cfg != NULL;
    const char* old_value = cfg_getstr(old_cfg, name);
    const char* new_value = cfg_getstr(new_cfg, name);
    if (old_value == NULL)
        return new_value != NULL;
    if (new_value != NULL)
        return strcmp(old_value, new_value) != 0;
    return true;
}

static bool setup_state(int argc, char** argv, postsrsd_t* state)
{
    postsrsd_t new_state;
    bool socketmap_rollback = false;
    init_state(&new_state);
    new_state.cfg = config_from_commandline(argc, argv);
    if (new_state.cfg == NULL)
        goto fail;
    if (cfg_getbool(new_state.cfg, "syslog"))
        log_enable_syslog();
    else
        log_disable_syslog();
    log_set_verbosity(cfg_getbool(new_state.cfg, "debug") ? LogDebug : LogInfo);
    new_state.srs = srs_from_config(new_state.cfg);
    if (new_state.srs == NULL)
        goto fail;
    const char* domains_file = cfg_getstr(new_state.cfg, "domains-file");
    if (cfg_getbool(new_state.cfg, "domains-file-watch")
        && NONEMPTY_STRING(domains_file))
    {
        new_state.file_watch = file_watch_create();
        if (new_state.file_watch == NULL)
        {
            log_error("failed to setup inotify watch");
            goto fail;
        }
        file_watch_if_modified(new_state.file_watch, domains_file,
                               on_file_watch_event);
    }
    if (!srs_domains_from_config(new_state.cfg, &new_state.srs_domain,
                                 &new_state.local_domains))
        goto fail;
    if (!unprivileged_user_from_config(new_state.cfg, &new_state.target_uid,
                                       &new_state.target_gid))
        goto fail;
    if (!check_unprivileged_work(&new_state))
        goto fail;
    if (config_changed_str(state->cfg, new_state.cfg, "socketmap"))
    {
        const char* value = cfg_getstr(new_state.cfg, "socketmap");
        if (NONEMPTY_STRING(value))
        {
            new_state.socketmap = endpoint_create(value);
            if (new_state.socketmap == NULL)
                goto fail;
        }
    }
    else
    {
        /* We want to keep the socketmap, so we move it from the old state
           before we actually commit to the new state. This needs to be
           rolled back if a configuration error occurs later. */
        new_state.socketmap = state->socketmap;
        state->socketmap = NULL;
        socketmap_rollback = true;
    }
    /* If we reached this point, the new configuration is valid, so we commit */
    finalize_state(state);
    *state = new_state;
    return true;
fail:
    /* If a configuration error occurs and the daemon was configured already, we
       want to keep the old functional configuration. */
    if (socketmap_rollback)
    {
        state->socketmap = new_state.socketmap;
        new_state.socketmap = NULL;
    }
    finalize_state(&new_state);
    return false;
}

int main(int argc, char** argv)
{
    postsrsd_t state;
    init_state(&state);
    FILE* pf = NULL;
    int milter_pid = 0;
    int exit_code = EXIT_FAILURE;
#ifdef HAVE_CLOSE_RANGE
    close_range(3, ~0U, 0);
#else
    for (int fd = 3; fd < 1024; ++fd)
        close(fd);
#endif
    signal(SIGALRM, SIG_IGN);
    signal(SIGUSR1, SIG_IGN);
    if (!setup_state(argc, argv, &state))
        goto shutdown;
    const char* milter_endpoint = cfg_getstr(state.cfg, "milter");
    if (NONEMPTY_STRING(milter_endpoint))
    {
        if (!milter_create(milter_endpoint))
            goto shutdown;
    }
    else
    {
        milter_endpoint = NULL;
    }
    const char* pid_file = cfg_getstr(state.cfg, "pid-file");
    if (NONEMPTY_STRING(pid_file))
    {
        pf = fopen(pid_file, "w");
        if (pf == NULL)
        {
            log_error("cannot open %s for writing", pid_file);
            goto shutdown;
        }
    }
    if (!daemonize(&state))
        goto shutdown;
    if (pf != NULL)
    {
        fprintf(pf, "%d", (int)getpid());
        fclose(pf);
        pf = NULL;
    }
    exit_code = EXIT_SUCCESS;
    if (state.socketmap != NULL)
    {
        if (NONEMPTY_STRING(milter_endpoint))
        {
            milter_pid = fork();
            if (milter_pid == 0)
            {
                if (drop_privileges(&state))
                {
                    milter_main(state.cfg, state.srs, state.srs_domain,
                                state.local_domains);
                }
                goto shutdown;
            }
        }
        signal(SIGHUP, on_sig_hup);
        signal(SIGTERM, on_sig_term);
        struct pollfd fds[5];
        unsigned num_fds = endpoint_prepare_poll(
            state.socketmap, fds, sizeof(fds) / sizeof(struct pollfd));
        num_fds += file_watch_prepare_poll(state.file_watch, fds + num_fds,
                                           sizeof(fds) / sizeof(struct pollfd)
                                               - num_fds);
        for (;;)
        {
            if (sig_hup_received || files_changed)
            {
                if (sig_hup_received)
                {
                    sig_hup_received = 0;
                    log_info("SIGHUP received, reloading configuration.");
                }
                if (files_changed)
                {
                    files_changed = false;
                    log_info("file change detected, reloading configuration.");
                }
                if (setup_state(argc, argv, &state))
                {
                    kill(0, SIGUSR1);
                    num_fds = endpoint_prepare_poll(
                        state.socketmap, fds,
                        sizeof(fds) / sizeof(struct pollfd));
                    num_fds += file_watch_prepare_poll(
                        state.file_watch, fds + num_fds,
                        sizeof(fds) / sizeof(struct pollfd) - num_fds);
                }
                else
                {
                    log_error("configuration error, keeping the old one");
                }
            }
            if (sig_term_received)
            {
                sig_term_received = 0;
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
                    if (fds[i].fd == file_watch_poll_fd(state.file_watch))
                    {
                        file_watch_process_events(state.file_watch);
                    }
                    else
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
                            handle_socketmap_client(&state, conn);
                            exit(EXIT_SUCCESS);
                        }
                        if (pid < 0)
                        {
                            log_perror(errno, "fork");
                        }
                        close(conn);
                    }
                }
            }
            waitpid(0, NULL, WNOHANG);
        }
    }
    else if (NONEMPTY_STRING(milter_endpoint))
    {
        milter_main(state.cfg, state.srs, state.srs_domain,
                    state.local_domains);
    }
shutdown:
    if (pf != NULL)
        fclose(pf);
    finalize_state(&state);
    kill(0, SIGUSR1);
    if (milter_pid > 0)
    {
        kill(milter_pid, SIGTERM);
        waitpid(milter_pid, NULL, 0);
        milter_pid = 0;
    }
    return exit_code;
}
