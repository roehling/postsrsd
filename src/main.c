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
static bool sd_notify_support = false;

#ifdef WITH_SECCOMP
#    include <seccomp.h>

static scmp_filter_ctx scmp_ctx;
#endif

struct postsrsd
{
    cfg_t* cfg;
    srs_t* srs;
    endpoint_t* socketmap;
    endpoint_t* milter;
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
    state->milter = NULL;
    state->srs_domain = NULL;
    state->local_domains = NULL;
    state->file_watch = NULL;
    state->target_uid = 0;
    state->target_gid = 0;
}

static bool init_seccomp()
{
#ifdef WITH_SECCOMP
    scmp_ctx = seccomp_init(SCMP_ACT_KILL_PROCESS);
    if (scmp_ctx == NULL)
        return false;
    /* Syscalls without database access */
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(fstat), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(read), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(write), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(alarm), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(sigreturn), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(rt_sigreturn), 0)
        < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(close), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(exit), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(exit_group), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(time), 0) < 0)
        goto fail;
#    ifdef WITH_SQLITE
    /* Syscalls for SQlite database access */
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(brk), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(getpid), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(geteuid), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(lseek), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(newfstatat), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(fcntl), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(openat), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(pread64), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(pwrite64), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(fdatasync), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(unlink), 0) < 0)
        goto fail;
#    endif
#    ifdef WITH_REDIS
    /* Syscalls for Redis database access */
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(sendto), 0) < 0)
        goto fail;
    if (seccomp_rule_add(scmp_ctx, SCMP_ACT_ALLOW, SCMP_SYS(recvfrom), 0) < 0)
        goto fail;
#    endif
    return true;
fail:
    seccomp_release(scmp_ctx);
    scmp_ctx = NULL;
    return false;
#else
    return false;
#endif
}

static void finalize_seccomp()
{
#ifdef WITH_SECCOMP
    if (scmp_ctx != NULL)
        seccomp_release(scmp_ctx);
    scmp_ctx = NULL;
#endif
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
    if (state->milter != NULL)
    {
        endpoint_close(state->milter);
        state->milter = NULL;
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

static bool prepare_client(postsrsd_t* state, int conn, FILE** fp_read,
                           FILE** fp_write, database_t** db)
{
    if (state == NULL || fp_read == NULL || fp_write == NULL || db == NULL)
        return false;
    *fp_read = NULL;
    *fp_write = NULL;
    *db = NULL;
#ifdef HAVE_FCNTL_H
    int flags = fcntl(conn, F_GETFL);
    if (flags & O_NONBLOCK)
    {
        if (fcntl(conn, F_SETFL, flags & ~O_NONBLOCK) < 0)
        {
            log_error("failed to make socket connection blocking");
            return false;
        }
    }
#endif
    *fp_write = fdopen(dup(conn), "w");
    if (*fp_write == NULL)
        return false;
    *fp_read = fdopen(conn, "r");
    if (*fp_read == NULL)
        return false;
    if (cfg_getint(state->cfg, "original-envelope") == SRS_ENVELOPE_DATABASE)
    {
        *db = database_connect(cfg_getstr(state->cfg, "envelope-database"),
                               false);
        if (*db == NULL)
            return false;
    }
    signal(SIGALRM, on_sigalrm);
    signal(SIGUSR1, on_sig_hup);
    signal(SIGTERM, SIG_DFL);
#ifdef WITH_SECCOMP
    if (cfg_getbool(state->cfg, "seccomp") && scmp_ctx != NULL
        && seccomp_load(scmp_ctx) < 0)
    {
        log_error("failed to activate seccomp sandboxing");
        return false;
    }
#endif
    return true;
}

static void handle_socketmap_client(postsrsd_t* state, int conn)
{
    FILE *fp_read, *fp_write;
    database_t* db;
    if (!prepare_client(state, conn, &fp_read, &fp_write, &db))
        exit(EXIT_FAILURE);
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

static void handle_milter_client(postsrsd_t* state, int conn)
{
#define MS_UNINITIALIZED 0
#define MS_READY         1
#define MS_PROCESSING    2
    FILE *fp_read, *fp_write;
    database_t* db;
    char buffer[512];
    ssize_t len;
    if (!prepare_client(state, conn, &fp_read, &fp_write, &db))
        exit(EXIT_FAILURE);
    if (sig_hup_received)
        return;
    int keep_alive = cfg_getint(state->cfg, "keep-alive");
    int milter_state = MS_UNINITIALIZED;
    char* sender = NULL;
    char* queue_id = NULL;
    list_t* recipients = list_create();
    for (;;)
    {
        timeout = 0;
        alarm(keep_alive);
        len = milter_receive(fp_read, buffer, sizeof(buffer));
        if (len == 0 || timeout)
            break;
        if (len < 0)
        {
            int err = errno;
            log_perror(err, "milter_receive");
            milter_send(fp_write, err == EMSGSIZE ? MILTER_DO_REJECT
                                                  : MILTER_DO_TEMPFAIL);
            break;
        }
        alarm(0);
        char action = buffer[0];
        const char* info = NULL;
        bool error = false;
        bool is_local = false;
        switch (action)
        {
            case MILTER_CMD_OPTNEG:
                if (milter_state != MS_UNINITIALIZED)
                {
                    log_error(
                        "%s: MTA initiated unexpected milter option "
                        "negotiation",
                        queue_id != NULL ? queue_id : "NOQUEUE");
                    milter_send(fp_write, MILTER_DO_TEMPFAIL);
                    break;
                }
                if (!milter_handle_optneg(fp_write, buffer + 1, len - 1))
                    goto done;
                milter_state = MS_READY;
                break;
            case MILTER_CMD_MACRO:
                set_string(&queue_id,
                           milter_find_macro("i", buffer + 1, len - 1));
                break;
            case MILTER_CMD_ABORT:
                milter_state = MS_READY;
                list_clear(recipients, free);
                set_string(&sender, NULL);
                set_string(&queue_id, NULL);
                if (sig_hup_received)
                    goto done;
                break;
            case MILTER_CMD_MAIL:
                if (milter_state != MS_READY)
                {
                    log_error("%s: MTA sent unexpected milter MAIL command",
                              queue_id != NULL ? queue_id : "NOQUEUE");
                    milter_send(fp_write, MILTER_DO_TEMPFAIL);
                    goto done;
                }
                milter_state = MS_PROCESSING;
                sender = strip_brackets(buffer + 1);
                if (!milter_send(fp_write, MILTER_DO_CONTINUE))
                    goto done;
                break;
            case MILTER_CMD_QUIT:
                goto done;
            case MILTER_CMD_RCPT:
                if (milter_state != MS_PROCESSING)
                {
                    log_error("%s: MTA sent unexpected milter RCPT command",
                              queue_id != NULL ? queue_id : "NOQUEUE");
                    milter_send(fp_write, MILTER_DO_TEMPFAIL);
                    goto done;
                }
                list_append(recipients, strip_brackets(buffer + 1));
                if (!milter_send(fp_write, MILTER_DO_CONTINUE))
                    goto done;
                break;
            case MILTER_CMD_EOB:
                if (milter_state != MS_PROCESSING)
                {
                    log_error("%s: MTA sent unexpected milter EOM command",
                              queue_id != NULL ? queue_id : "NOQUEUE");
                    milter_send(fp_write, MILTER_DO_TEMPFAIL);
                    goto done;
                }
                is_local = true;
                for (size_t i = 0; i < list_size(recipients); ++i)
                {
                    char* rcpt = list_get(recipients, i);
                    char* rewritten =
                        postsrsd_reverse(rcpt, state->srs, db, &error, &info);
                    if (rewritten)
                    {
                        char* bracketed_old_rcpt = add_brackets(rcpt);
                        char* bracketed_new_rcpt = add_brackets(rewritten);
                        if (!milter_send_str(fp_write, MILTER_DO_DELRCPT,
                                             bracketed_old_rcpt))
                            goto done;
                        if (!milter_send_str(fp_write, MILTER_DO_ADDRCPT,
                                             bracketed_new_rcpt))
                            goto done;
                        free(bracketed_old_rcpt);
                        free(bracketed_new_rcpt);
                        char* domain = strchr(rewritten, '@');
                        if (domain != NULL
                            && !domain_set_contains(state->local_domains,
                                                    domain + 1))
                            is_local = false;
                        free(rewritten);
                    }
                    else if (error)
                    {
                        if (!milter_send_str(fp_write, MILTER_DO_REJECT, info))
                            goto done;
                        goto cleanup;
                    }
                    else
                    {
                        char* domain = strchr(rcpt, '@');
                        if (domain != NULL
                            && !domain_set_contains(state->local_domains,
                                                    domain + 1))
                            is_local = false;
                    }
                }
                if (!is_local)
                {
                    char* rewritten = postsrsd_forward(
                        sender, state->srs_domain, state->srs, db,
                        state->local_domains, &error, &info);
                    if (rewritten)
                    {
                        char* bracketed_new_sender = add_brackets(rewritten);
                        if (!milter_send_str(fp_write, MILTER_DO_CHGFROM,
                                             bracketed_new_sender))
                            goto done;
                        free(bracketed_new_sender);
                        free(rewritten);
                    }
                    else if (error)
                    {
                        if (!milter_send_str(fp_write, MILTER_DO_REJECT, info))
                            goto done;
                        goto cleanup;
                    }
                }
                else
                {
                    log_info(
                        "%s: <%s> not rewritten: all recipients are in local "
                        "domains",
                        queue_id != NULL ? queue_id : "NOQUEUE", sender);
                }
                if (!milter_send(fp_write, MILTER_DO_ACCEPT))
                    goto done;
cleanup:
                set_string(&sender, NULL);
                list_clear(recipients, free);
                set_string(&queue_id, NULL);
                milter_state = MS_READY;
                break;
            default:
                log_warn("%s: MTA sent unexpected milter command '%c'",
                         queue_id != NULL ? queue_id : "NOQUEUE", action);
                if (!milter_send(fp_write, MILTER_DO_CONTINUE))
                    goto done;
                break;
        }
        fflush(fp_write);
    }
done:
    fflush(fp_write);
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
    bool milter_rollback = false;
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
    if (config_changed_str(state->cfg, new_state.cfg, "milter"))
    {
        const char* value = cfg_getstr(new_state.cfg, "milter");
        if (NONEMPTY_STRING(value))
        {
            new_state.milter = endpoint_create(value);
            if (new_state.milter == NULL)
                goto fail;
        }
    }
    else
    {
        /* Same logic applies as with socketmap */
        new_state.milter = state->milter;
        state->milter = NULL;
        milter_rollback = true;
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
    if (milter_rollback)
    {
        state->milter = new_state.milter;
        new_state.milter = NULL;
    }
    finalize_state(&new_state);
    return false;
}

int main(int argc, char** argv)
{
    postsrsd_t state;
    init_state(&state);
    if (!init_seccomp() && cfg_getbool(state.cfg, "seccomp"))
        log_warn("seccomp sandboxing is unavailable");
    FILE* pf = NULL;
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
    signal(SIGHUP, on_sig_hup);
    signal(SIGTERM, on_sig_term);
    sd_notify_support = sd_notify("READY=1\nMAINPID=%d", getpid());
    struct pollfd fds[10];
    unsigned remaining_fds = sizeof(fds) / sizeof(struct pollfd);
    unsigned num_fds = 0;
    unsigned num_socketmap_fds =
        endpoint_prepare_poll(state.socketmap, fds + num_fds, remaining_fds);
    remaining_fds -= num_socketmap_fds;
    num_fds += num_socketmap_fds;
    unsigned num_milter_fds =
        endpoint_prepare_poll(state.milter, fds + num_fds, remaining_fds);
    remaining_fds -= num_milter_fds;
    num_fds += num_milter_fds;
    num_fds +=
        file_watch_prepare_poll(state.file_watch, fds + num_fds, remaining_fds);
    for (;;)
    {
        if (sig_hup_received || files_changed)
        {
            if (sd_notify_support)
            {
                struct timespec tp;
                clock_gettime(CLOCK_MONOTONIC, &tp);
                sd_notify("RELOADING=1\nMONOTONIC_USEC=%ld",
                          1000000l * tp.tv_sec + tp.tv_nsec / 1000l);
            }
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
                num_fds = 0;
                remaining_fds = sizeof(fds) / sizeof(struct pollfd);
                num_socketmap_fds = endpoint_prepare_poll(
                    state.socketmap, fds + num_fds, remaining_fds);
                remaining_fds -= num_socketmap_fds;
                num_fds += num_socketmap_fds;
                num_milter_fds = endpoint_prepare_poll(
                    state.milter, fds + num_fds, remaining_fds);
                remaining_fds -= num_milter_fds;
                num_fds += num_milter_fds;
                num_fds += file_watch_prepare_poll(
                    state.file_watch, fds + num_fds, remaining_fds);
            }
            else
            {
                log_error("configuration error, keeping the old one");
            }
            if (sd_notify_support)
                sd_notify("READY=1");
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
                        if (i < num_socketmap_fds)
                            handle_socketmap_client(&state, conn);
                        else
                            handle_milter_client(&state, conn);
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
shutdown:
    if (pf != NULL)
        fclose(pf);
    finalize_state(&state);
    finalize_seccomp();
    kill(0, SIGUSR1);
    return exit_code;
}
