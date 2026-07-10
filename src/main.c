/* PostSRSd - Sender Rewriting Scheme daemon for Postfix
 * Copyright 2012-2026 Timo Röhling <timo@gaussglocke.de>
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
#ifdef HAVE_SYS_WAIT_H
#    include <sys/wait.h>
#endif
#ifdef HAVE_SIGNAL_H
#    include <signal.h>
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

#define PAYLOAD_SIZE MILTER_PAYLOAD_SIZE

#define FD_UNUSED    0
#define FD_SOCKETMAP 1
#define FD_MILTER    2
#define FD_WATCH     3

static volatile sig_atomic_t timeout = 0;
static volatile sig_atomic_t reload_requested = 0, shutdown_requested = 0;
static bool files_changed = false, files_changed_unsafe = false;
static time_t last_file_watch_event = 0;
static bool sd_notify_support = false;
static sandbox_t* sandbox = NULL;

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

static void finalize_state(postsrsd_t* state)
{
    if (state->file_watch != NULL)
    {
        file_watch_destroy(state->file_watch);
        state->file_watch = NULL;
    }
    if (state->socketmap != NULL)
    {
        endpoint_destroy(state->socketmap);
        state->socketmap = NULL;
    }
    if (state->milter != NULL)
    {
        endpoint_destroy(state->milter);
        state->milter = NULL;
    }
    string_set(&state->srs_domain, NULL);
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
            if (cfg_getbool(state->cfg, "seccomp") && sandbox != NULL
                && !sandbox_enable(sandbox))
            {
                log_fatal("failed to enable sandboxing for worker processes");
            }
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

static void on_timeout(int signum)
{
    timeout = signum;
}

static void on_reload_requested(int signum)
{
    reload_requested = signum;
}

static void on_shutdown_requested(int signum)
{
    shutdown_requested = signum;
}

static bool prepare_client(postsrsd_t* state, int conn, database_t** db)
{
    if (state == NULL || db == NULL)
        return false;
    *db = NULL;
    if (!drop_privileges(state))
        return false;
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
    if (cfg_getint(state->cfg, "original-envelope") == SRS_ENVELOPE_DATABASE)
    {
        *db = database_connect(cfg_getstr(state->cfg, "envelope-database"),
                               false);
        if (*db == NULL)
            return false;
    }
    signal(SIGALRM, on_timeout);
    signal(SIGUSR1, on_reload_requested);
    signal(SIGTERM, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    if (cfg_getbool(state->cfg, "seccomp") && sandbox != NULL
        && !sandbox_enable(sandbox))
    {
        log_error("failed to activate seccomp sandboxing");
        return false;
    }
    return true;
}

static void handle_socketmap_client(postsrsd_t* state, int conn)
{
    database_t* db;
    if (!prepare_client(state, conn, &db))
        exit(EXIT_FAILURE);
    const bool always_rewrite = cfg_getbool(state->cfg, "always-rewrite");
    const int keep_alive = cfg_getint(state->cfg, "keep-alive");
    for (;;)
    {
        char buffer[1024];
        size_t len;
        char* addr;
        bool error;
        char* eob;
        if (reload_requested)
            break;
        timeout = 0;
        alarm(keep_alive);
        char* request = netstring_read(conn, buffer, sizeof(buffer), &len);
        if (timeout)
            break;
        if (request == NULL)
        {
            if (len != 0)
            {
                netstring_write(conn, "PERM Invalid query.", 19);
                log_error("invalid socketmap query, closing connection");
            }
            break;
        }
        alarm(0);
        char* query_type = strtok_r(request, " ", &addr);
        if (query_type == NULL)
        {
            netstring_write(conn, "PERM Invalid query.", 19);
            log_error("invalid socketmap query, closing connection");
            break;
        }
        if (len > PAYLOAD_SIZE + (size_t)(addr - request))
        {
            netstring_write(conn, "PERM Too big.", 13);
            log_warn("socketmap query is too big");
            continue;
        }
        char* rewritten = NULL;
        const char* info = NULL;
        if (strcmp(query_type, "forward") == 0)
        {
            rewritten =
                postsrsd_forward(addr, state->srs_domain, state->srs, db,
                                 always_rewrite ? NULL : state->local_domains,
                                 &error, &info, "socketmap");
        }
        else if (strcmp(query_type, "reverse") == 0)
        {
            rewritten = postsrsd_reverse(addr, state->srs, db, &error, &info,
                                         "socketmap");
        }
        else
        {
            error = true;
            info = "Invalid map.";
            log_warn("invalid key in socketmap query");
        }
        if (rewritten)
        {
            eob = stpncpy(stpcpy(buffer, "OK "), rewritten, sizeof(buffer) - 4);
            free(rewritten);
            netstring_write(conn, buffer, eob - buffer);
        }
        else
        {
            if (error)
            {
                eob = stpcpy(buffer, "PERM ");
            }
            else
            {
                eob = stpcpy(buffer, "NOTFOUND ");
            }
            if (info)
                eob = stpncpy(eob, info, sizeof(buffer) - 9);
            netstring_write(conn, buffer, eob - buffer);
        }
    }
    database_disconnect(db);
}

static void handle_milter_client(postsrsd_t* state, int conn)
{
#define MILTER_AWAIT_OPTNEG      0
#define MILTER_AWAIT_MAIL        1
#define MILTER_AWAIT_RCPT        2
#define MILTER_AWAIT_RCPT_OR_EOM 3
    database_t* db;
    char buffer[PAYLOAD_SIZE];
    size_t len, truncated;
    if (!prepare_client(state, conn, &db))
        exit(EXIT_FAILURE);
    if (reload_requested)
        return;
    const bool always_rewrite = cfg_getbool(state->cfg, "always-rewrite");
    const bool rewrite_local = cfg_getbool(state->cfg, "milter-rewrite-local");
    const int keep_alive = cfg_getint(state->cfg, "keep-alive");
    int milter_state = MILTER_AWAIT_OPTNEG;
    char* queue_id = NULL;
    string_set(&queue_id, strdup("NOQUEUE"));
    list_t* sender = list_create();
    list_t* recipients = list_create();
    for (;;)
    {
        timeout = 0;
        alarm(keep_alive);
        len = milter_receive(conn, buffer, sizeof(buffer), &truncated);
        if (len == 0 || timeout)
            break;
        alarm(0);
        char action = buffer[0];
        char* addr = NULL;
        const char* info = NULL;
        bool error = false;
        bool is_local = false;
        switch (action)
        {
            case MILTER_CMD_OPTNEG:
                if (milter_state != MILTER_AWAIT_OPTNEG)
                {
                    log_error(
                        "%s: MTA initiated unexpected milter option "
                        "negotiation",
                        queue_id);
                    if (!milter_tempfail(conn))
                        goto done;
                    goto cleanup;
                }
                if (!milter_handle_optneg(conn, buffer + 1, len - 1))
                    goto done;
                milter_state = MILTER_AWAIT_MAIL;
                break;
            case MILTER_CMD_MACRO:
                if (len > 2)
                    string_set(&queue_id,
                               milter_parse_macros("i", buffer + 2, len - 2));
                break;
            case MILTER_CMD_MAIL:
                if (milter_state != MILTER_AWAIT_MAIL)
                {
                    log_error("%s: MTA sent unexpected milter MAIL command",
                              queue_id);
                    if (!milter_tempfail(conn))
                        goto done;
                    goto cleanup;
                }
                if (truncated > 0)
                {
                    log_error("%s: MTA sent oversized milter MAIL command",
                              queue_id);
                    if (!milter_reject(conn))
                        goto done;
                    goto cleanup;
                }
                milter_state = MILTER_AWAIT_RCPT;
                milter_parse_str_list(sender, buffer + 1, len - 1);
                if (list_size(sender) < 1)
                {
                    log_error("%s: MTA sent empty milter MAIL command",
                              queue_id);
                    if (!milter_reject(conn))
                        goto done;
                    goto cleanup;
                }
                addr = milter_parse_address(list_get(sender, 0));
                if (addr == NULL)
                {
                    log_error("%s: MTA sent malformed milter MAIL command",
                              queue_id);
                    if (!milter_reject(conn))
                        goto done;
                    goto cleanup;
                }
                list_replace_at(sender, 0, addr, free);
                if (!milter_continue(conn))
                    goto done;
                break;
            case MILTER_CMD_QUIT:
                goto done;
            case MILTER_CMD_RCPT:
                if (milter_state != MILTER_AWAIT_RCPT
                    && milter_state != MILTER_AWAIT_RCPT_OR_EOM)
                {
                    log_error("%s: MTA sent unexpected milter RCPT command",
                              queue_id);
                    if (!milter_tempfail(conn))
                        goto done;
                    goto cleanup;
                }
                if (truncated > 0)
                {
                    log_error("%s: MTA sent oversized milter RCPT command",
                              queue_id);
                    if (!milter_reject(conn))
                        goto done;
                    goto cleanup;
                }
                list_append(recipients, strndup(buffer + 1, len - 1));
                if (!milter_continue(conn))
                    goto done;
                milter_state = MILTER_AWAIT_RCPT_OR_EOM;
                break;
            case MILTER_CMD_EOM:
                if (milter_state != MILTER_AWAIT_RCPT_OR_EOM)
                {
                    log_error("%s: MTA sent unexpected milter EOM command",
                              queue_id);
                    if (!milter_tempfail(conn))
                        goto done;
                    goto cleanup;
                }
                if (list_size(sender) == 0)
                {
                    log_error("%s: no sender envelope address", queue_id);
                    if (!milter_reject(conn))
                        goto done;
                    goto cleanup;
                }
                if (list_size(recipients) == 0)
                {
                    log_error("%s: no recipient address", queue_id);
                    if (!milter_reject(conn))
                        goto done;
                    goto cleanup;
                }
                is_local = true;
                for (size_t i = 0; i < list_size(recipients); ++i)
                {
                    char* old_rcpt = list_get(recipients, i);
                    addr = milter_parse_address_buf(old_rcpt, buffer,
                                                    sizeof(buffer));
                    if (addr == NULL)
                    {
                        log_error("%s: invalid recipient: %s", queue_id,
                                  old_rcpt);
                        if (!milter_reject(conn))
                            goto done;
                        goto cleanup;
                    }
                    char* rewritten = postsrsd_reverse(addr, state->srs, db,
                                                       &error, &info, queue_id);
                    if (rewritten)
                    {
                        if (!milter_send_str(conn, MILTER_DO_DELRCPT, old_rcpt))
                            goto done;
                        if (!milter_send_str(conn, MILTER_DO_ADDRCPT,
                                             rewritten))
                            goto done;
                        char* domain = strchr(rewritten, '@');
                        if (domain != NULL
                            && !domain_set_contains(state->local_domains,
                                                    domain + 1))
                            is_local = false;
                        free(rewritten);
                    }
                    else if (error)
                    {
                        if (!milter_reject(conn))
                            goto done;
                        goto cleanup;
                    }
                    else
                    {
                        char* domain = strchr(addr, '@');
                        if (domain != NULL)
                        {
                            if (SRS_IS_SRS_ADDRESS(addr)
                                && strcasecmp(domain + 1, state->srs_domain)
                                       == 0)
                            {
                                log_info(
                                    "%s: rejecting invalid SRS address <%s>",
                                    queue_id, addr);
                                if (!milter_reject(conn))
                                    goto done;
                                goto cleanup;
                            }
                            if (!domain_set_contains(state->local_domains,
                                                     domain + 1))
                                is_local = false;
                        }
                    }
                }
                if (!is_local || rewrite_local || always_rewrite)
                {
                    char* rewritten = postsrsd_forward(
                        list_get(sender, 0), state->srs_domain, state->srs, db,
                        always_rewrite ? NULL : state->local_domains, &error,
                        &info, queue_id);
                    if (rewritten)
                    {
                        list_replace_at(sender, 0, rewritten, free);
                        if (!milter_send_str_list(conn, MILTER_DO_CHGFROM,
                                                  sender))
                            goto done;
                    }
                    else if (error)
                    {
                        if (!milter_reject(conn))
                            goto done;
                        goto cleanup;
                    }
                }
                else
                {
                    log_info(
                        "%s: <%s> not rewritten: all recipients are in local "
                        "domains",
                        queue_id, (char*)list_get(sender, 0));
                }
                if (!milter_accept(conn))
                    goto done;
                goto cleanup;
            case MILTER_CMD_ABORT:
                log_info("%s: MTA aborted transaction", queue_id);
cleanup:
                list_clear(sender, free);
                list_clear(recipients, free);
                string_set(&queue_id, strdup("NOQUEUE"));
                if (milter_state != MILTER_AWAIT_OPTNEG)
                    milter_state = MILTER_AWAIT_MAIL;
                if (reload_requested)
                    goto done;
                break;
            default:
                log_warn("%s: MTA sent unexpected milter command", queue_id);
                if (milter_state != MILTER_AWAIT_OPTNEG)
                {
                    if (!milter_continue(conn))
                        goto done;
                }
                break;
        }
    }
done:
    list_clear(sender, free);
    list_clear(recipients, free);
    string_set(&queue_id, NULL);
    database_disconnect(db);
}

static void on_file_watch_event(const char* path, unsigned what, size_t cookie)
{
    MAYBE_UNUSED(path);
    MAYBE_UNUSED(cookie);
    if (what & FW_MODIFIED)
    {
        files_changed = true;
        files_changed_unsafe = false;
    }
    else if (what & FW_CREATED)
    {
        files_changed_unsafe = true;
    }
    if (what & FW_CHANGING)
    {
        files_changed_unsafe = false;
    }
    time(&last_file_watch_event);
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
    if (cfg_getbool(state->cfg, "syslog"))
        log_enable_syslog();
    else
        log_disable_syslog();
    log_set_verbosity(cfg_getbool(state->cfg, "debug") ? LogDebug : LogInfo);
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

static size_t setup_poll(postsrsd_t* state, struct pollfd* fds, int* fd_types,
                         size_t max_fds)
{
    size_t num_fds = 0;
    size_t remaining_fds = max_fds;
    size_t num_socketmap_fds =
        endpoint_prepare_poll(state->socketmap, fds + num_fds, remaining_fds);
    for (size_t i = num_fds; i < num_fds + num_socketmap_fds; ++i)
        fd_types[i] = FD_SOCKETMAP;
    remaining_fds -= num_socketmap_fds;
    num_fds += num_socketmap_fds;
    size_t num_milter_fds =
        endpoint_prepare_poll(state->milter, fds + num_fds, remaining_fds);
    for (size_t i = num_fds; i < num_fds + num_milter_fds; ++i)
        fd_types[i] = FD_MILTER;
    remaining_fds -= num_milter_fds;
    num_fds += num_milter_fds;
    size_t num_watch_fds = file_watch_prepare_poll(
        state->file_watch, fds + num_fds, remaining_fds);
    for (size_t i = num_fds; i < num_fds + num_watch_fds; ++i)
        fd_types[i] = FD_WATCH;
    remaining_fds -= num_watch_fds;
    num_fds += num_watch_fds;
    for (size_t i = num_fds; i < max_fds; ++i)
        fd_types[i] = FD_UNUSED;
    return num_fds;
}

int main(int argc, char** argv)
{
    postsrsd_t state;
    init_state(&state);
    FILE* pf = NULL;
    pid_set_t* P = NULL;
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
    sandbox = sandbox_init();
    if (sandbox == NULL)
        log_warn("seccomp sandbox is unavailable");
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
    P = pid_set_create();
    if (P == NULL)
        goto shutdown;
    if (!daemonize(&state))
        goto shutdown;
    if (pf != NULL)
    {
        fprintf(pf, "%d", (int)getpid());
        fclose(pf);
        pf = NULL;
    }
    exit_code = EXIT_SUCCESS;
    signal(SIGHUP, on_reload_requested);
    signal(SIGTERM, on_shutdown_requested);
    signal(SIGINT, on_shutdown_requested);
    sd_notify_support = sd_notify("READY=1\nMAINPID=%d", (int)getpid());
    struct pollfd fds[16];
    int fd_types[sizeof(fds) / sizeof(struct pollfd)];
    size_t num_fds =
        setup_poll(&state, fds, fd_types, sizeof(fds) / sizeof(struct pollfd));
    pid_t pid;
    int child_status;
    for (;;)
    {
        if (files_changed_unsafe && time(NULL) - last_file_watch_event >= 1)
        {
            files_changed = true;
        }
        if (reload_requested || files_changed)
        {
            if (sd_notify_support)
            {
                struct timespec tp;
                clock_gettime(CLOCK_MONOTONIC, &tp);
                sd_notify("RELOADING=1\nMONOTONIC_USEC=%ld",
                          (long)(1000000l * tp.tv_sec + tp.tv_nsec / 1000l));
            }
            if (reload_requested)
            {
                log_info("Signal %d received, reloading configuration.",
                         (int)reload_requested);
                reload_requested = 0;
            }
            if (files_changed)
            {
                files_changed = false;
                if (files_changed_unsafe)
                {
                    log_warn(
                        "trying to reload configuration after unsafe update");
                    files_changed_unsafe = false;
                }
                else
                    log_info("file change detected, reloading configuration.");
            }
            if (setup_state(argc, argv, &state))
            {
                pid_set_kill(P, SIGUSR1);
                num_fds = setup_poll(&state, fds, fd_types,
                                     sizeof(fds) / sizeof(struct pollfd));
            }
            else
            {
                log_error("configuration error, rolling back changes");
            }
            if (sd_notify_support)
                sd_notify("READY=1");
        }
        if (shutdown_requested)
        {
            log_info("Signal %d received. shutting down.",
                     (int)shutdown_requested);
            shutdown_requested = 0;
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
                    pid = fork();
                    if (pid == 0)
                    {
                        endpoint_release(state.socketmap);
                        state.socketmap = NULL;
                        endpoint_release(state.milter);
                        state.milter = NULL;
                        switch (fd_types[i])
                        {
                            case FD_SOCKETMAP:
                                handle_socketmap_client(&state, conn);
                                break;
                            case FD_MILTER:
                                handle_milter_client(&state, conn);
                                break;
                            default:
                                log_error("socket dispatch error");
                                exit(EXIT_FAILURE);
                        }
                        finalize_state(&state);
                        sandbox_release(sandbox);
                        pid_set_destroy(P);
                        exit(EXIT_SUCCESS);
                    }
                    if (pid > 0)
                    {
                        pid_set_add(P, pid);
                    }
                    else
                    {
                        log_perror(errno, "fork");
                    }
                    close(conn);
                }
            }
        }
        do
        {
            pid = waitpid(0, &child_status, WNOHANG);
            if (pid > 0)
            {
                if (WEXITSTATUS(child_status) != EXIT_SUCCESS)
                {
                    log_warn("child process %d exited with status code %d", pid,
                             WEXITSTATUS(child_status));
                }
                pid_set_remove(P, pid);
            }
        } while (pid > 0);
    }
shutdown:
    if (pf != NULL)
        fclose(pf);
    finalize_state(&state);
    sandbox_release(sandbox);
    pid_set_kill(P, SIGTERM);
    pid_set_wait(P);
    pid_set_destroy(P);
    return exit_code;
}
