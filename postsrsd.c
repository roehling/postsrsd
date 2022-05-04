/* PostSRSd - Sender Rewriting Scheme daemon for Postfix
 * Copyright (c) 2012-2020 Timo Röhling <timo@gaussglocke.de>
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

/* This program uses the libsrs2 library. The relevant source
 * files have been added to this distribution. */

#include "srs2.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <pwd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef HAVE_SYS_TIME_H
#    include <sys/time.h>
#endif
#ifdef HAVE_TIME_H
#    include <time.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#    include <sys/wait.h>
#endif
#ifdef HAVE_WAIT_H
#    include <wait.h>
#endif
#include <syslog.h>

/* Prototype for milter function */
int milter_main(char *oconn, srs_t *srs_in, const char *domain_in,
                const char **excludes_in);

static char *self = NULL;

static size_t bind_service(const char *listen_addr, const char *service,
                           int family, int *socks, size_t max_socks)
{
    struct addrinfo *addr, *it;
    struct addrinfo hints;
    int err, sock, flags;
    size_t count = 0;
    static const int one = 1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM;

    err = getaddrinfo(listen_addr, service, &hints, &addr);
    if (err != 0)
    {
        fprintf(stderr, "%s: bind_service(%s): %s\n", self, service,
                gai_strerror(err));
        return count;
    }
    sock = -1;
    for (it = addr; it; it = it->ai_next)
    {
        if (max_socks == 0)
            break;
        sock = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (sock < 0)
            goto fail;
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0)
            goto fail;
        if (bind(sock, it->ai_addr, it->ai_addrlen) < 0)
            goto fail;
        if (listen(sock, 10) < 0)
            goto fail;
        flags = fcntl(sock, F_GETFL, 0);
        if (flags < 0)
            goto fail;
        if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0)
            goto fail;
        *socks++ = sock;
        ++count;
        --max_socks;
        continue;
    fail:
        err = errno;
        if (sock >= 0)
            close(sock);
    }
    freeaddrinfo(addr);
    if (count == 0)
        fprintf(stderr, "%s: bind_service(%s): %s\n", self, service,
                strerror(err));
    return count;
}

static int is_hexdigit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')
           || (c >= 'A' && c <= 'F');
}

static char hex2num(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return 0;
}

static char num2hex(char c)
{
    if (c < 10)
        return '0' + c;
    return 'a' + c - 10;
}

static char hex2char(const char *s)
{
    return 16 * hex2num(s[0]) + hex2num(s[1]);
}

static void char2hex(char c, char *buf)
{
    buf[0] = num2hex((c >> 4) & 0xf);
    buf[1] = num2hex((c)&0xf);
}

static char *url_decode(char *buf, size_t len, const char *input)
{
    char *output = buf;
    if (!input || !output || len == 0)
        return NULL;

    while (*input && --len)
    {
        if (*input == '%')
        {
            if (is_hexdigit(input[1]) && is_hexdigit(input[2]))
            {
                *output++ = hex2char(++input);
                input += 2;
            }
            else
            {
                return 0;
            }
        }
        else
        {
            *output++ = *input++;
        }
    }
    *output = 0;
    return buf;
}

static char *url_encode(char *buf, size_t len, const char *input)
{
    char *output = buf;
    if (!input || !output || len == 0)
        return NULL;
    while (*input && --len)
    {
        if (!isascii(*input) || !isgraph(*input) || *input == '%')
        {
            if (len <= 2)
                break;
            *output++ = '%';
            char2hex(*input++, output);
            output += 2;
            len -= 2;
        }
        else
        {
            *output++ = *input++;
        }
    }
    *output = 0;
    return buf;
}

static void handle_forward(srs_t *srs, FILE *fp, const char *address,
                           const char *domain, const char **excludes)
{
    int result;
    size_t addrlen;
    char value[1024];
    char outputbuf[1024], *output;

    addrlen = strlen(address);
    for (; *excludes; excludes++)
    {
        size_t len;
        len = strlen(*excludes);
        if (len >= addrlen)
            continue;
        if (strcasecmp(*excludes, &address[addrlen - len]) == 0
            && (**excludes == '.' || address[addrlen - len - 1] == '@'))
        {
            syslog(LOG_MAIL | LOG_INFO,
                   "srs_forward: <%s> not rewritten: Domain excluded by policy",
                   address);
            fputs("500 Domain excluded by policy\n", fp);
            fflush(fp);
            return;
        }
    }
    if (srs_reverse(srs, value, sizeof(value), address) == SRS_SUCCESS)
    {
        fprintf(fp, "500 Already rewritten\n");
        fflush(fp);
        syslog(LOG_MAIL | LOG_NOTICE,
               "srs_forward: <%s> not rewritten: Valid SRS address for <%s>",
               address, value);
        return;
    }
    result = srs_forward(srs, value, sizeof(value), address, domain);
    if (result == SRS_SUCCESS)
    {
        output = url_encode(outputbuf, sizeof(outputbuf), value);
        fprintf(fp, "200 %s\n", output);
        if (strcmp(address, value) != 0)
            syslog(LOG_MAIL | LOG_INFO, "srs_forward: <%s> rewritten as <%s>",
                   address, value);
    }
    else
    {
        fprintf(fp, "500 %s\n", srs_strerror(result));
        if (result != SRS_ENOTREWRITTEN)
            syslog(LOG_MAIL | LOG_INFO, "srs_forward: <%s> not rewritten: %s",
                   address, srs_strerror(result));
    }
    fflush(fp);
}

static void handle_reverse(srs_t *srs, FILE *fp, const char *address,
                           const char *domain __attribute__((unused)),
                           const char **excludes __attribute__((unused)))
{
    int result;
    char value[1024];
    char outputbuf[1024], *output;
    result = srs_reverse(srs, value, sizeof(value), address);
    if (result == SRS_SUCCESS)
    {
        output = url_encode(outputbuf, sizeof(outputbuf), value);
        fprintf(fp, "200 %s\n", output);
        syslog(LOG_MAIL | LOG_INFO, "srs_reverse: <%s> rewritten as <%s>",
               address, value);
    }
    else
    {
        fprintf(fp, "500 %s\n", srs_strerror(result));
        if (result != SRS_ENOTREWRITTEN && result != SRS_ENOTSRSADDRESS)
            syslog(LOG_MAIL | LOG_INFO, "srs_reverse: <%s> not rewritten: %s",
                   address, srs_strerror(result));
    }
    fflush(fp);
}

static void show_help()
{
    fprintf(
        stdout,
        "Sender Rewriting Scheme implementation for Postfix.\n\n"
        "Implements two TCP lookup tables to rewrite mail addresses\n"
        "as needed. The forward SRS is for sender envelope addresses\n"
        "to prevent SPF-related bounces. The reverse SRS is for\n"
        "recipient envelope addresses so that bounced mails can be\n"
        "routed back to their original sender.\n"
        "\n"
        "Usage: %s -s<file> -d<domain> [other options]\n"
        "Options:\n"
        "   -s<file>       read secrets from file (required)\n"
        "   -d<domain>     set domain name for rewrite (required)\n"
        "   -a<char>       set first separator character which can be one of: "
        "-=+ (default: =)\n"
        "   -n<num>        length of hash to be used in rewritten addresses "
        "(default: 4)\n"
        "   -N<num>        minimum length of hash to accept for validation "
        "(default: 4)\n"
        "   -l<addr>       set socket listen address (default: 127.0.0.1)\n"
        "   -f<port>       set port for the forward SRS lookup (default: "
        "10001)\n"
        "   -r<port>       set port for the reverse SRS lookup (default: "
        "10002)\n"
        "   -p<pidfile>    write process ID to pidfile (default: none)\n"
        "   -c<dir>        chroot to <dir> (default: none)\n"
        "   -u<user>       switch user id after port bind (default: none)\n"
        "   -t<seconds>    timeout for idle client connections (default: "
        "1800)\n"
        "   -X<domain>     exclude additional domain from address rewriting\n"
        "   -A             always rewrite addresses\n"
        "   -e             attempt to read above parameters from environment\n"
        "   -m             run in milter (instead of canonical) protocol mode\n"
        "   -D             fork into background\n"
        "   -4             force IPv4 socket (default: any)\n"
        "   -6             force IPv6 socket (default: any)\n"
        "   -h             show this help\n"
        "   -v             show version\n"
        "\n",
        self);
}

typedef void (*handle_t)(srs_t *, FILE *, const char *, const char *,
                         const char **);

int main(int argc, char **argv)
{
    int opt, timeout = 1800, family = AF_UNSPEC, hashlength = 0, hashmin = 0;
    int daemonize = FALSE, always_rewrite = FALSE;
    char *listen_addr = NULL, *forward_service = NULL, *reverse_service = NULL,
         *milter_service = NULL, *user = NULL, *domain = NULL,
         *chroot_dir = NULL;
    char separator = '=';
    char *secret_file = NULL, *pid_file = NULL;
    FILE *pf = NULL, *sf = NULL;
    struct passwd *pwd = NULL;
    char secretbuf[1024], *secret = NULL;
    char *tmp;
    time_t now;
    srs_t *srs;
    const char **excludes;
    size_t s1 = 0, s2 = 1;
    struct pollfd fds[4];
    size_t socket_count = 0, sc;
    int sockets[4] = {-1, -1, -1, -1};
    handle_t handler[4] = {0, 0, 0, 0};
    int fd, maxfd;

    excludes = (const char **)calloc(1, sizeof(char *));
    tmp = strrchr(argv[0], '/');
    if (tmp)
        self = strdup(tmp + 1);
    else
        self = strdup(argv[0]);

    while ((opt = getopt(argc, argv, "46d:a:l:f:r:m:s:n:N:u:t:p:c:X::ADhev"))
           != -1)
    {
        switch (opt)
        {
            case '?':
                return EXIT_FAILURE;
            case '4':
                family = AF_INET;
                break;
            case '6':
                family = AF_INET6;
                break;
            case 'd':
                domain = strdup(optarg);
                break;
            case 'a':
                separator = *optarg;
                break;
            case 'l':
                listen_addr = strdup(optarg);
                break;
            case 'f':
                forward_service = strdup(optarg);
                break;
            case 'r':
                reverse_service = strdup(optarg);
                break;
            case 'm':
                milter_service = strdup(optarg);
                break;
            case 't':
                timeout = atoi(optarg);
                break;
            case 's':
                secret_file = strdup(optarg);
                break;
            case 'n':
                hashlength = atoi(optarg);
                break;
            case 'N':
                hashmin = atoi(optarg);
                break;
            case 'p':
                pid_file = strdup(optarg);
                break;
            case 'u':
                user = strdup(optarg);
                break;
            case 'c':
                chroot_dir = strdup(optarg);
                break;
            case 'D':
                daemonize = TRUE;
                break;
            case 'A':
                always_rewrite = TRUE;
                break;
            case 'h':
                show_help();
                return EXIT_SUCCESS;
            case 'X':
                if (optarg != NULL)
                {
                    tmp = strtok(optarg, ",; \t\r\n");
                    while (tmp)
                    {
                        if (s1 + 1 >= s2)
                        {
                            s2 *= 2;
                            excludes = (const char **)realloc(
                                excludes, s2 * sizeof(char *));
                            if (excludes == NULL)
                            {
                                fprintf(stderr, "%s: Out of memory\n\n", self);
                                return EXIT_FAILURE;
                            }
                        }
                        excludes[s1++] = strdup(tmp);
                        tmp = strtok(NULL, ",; \t\r\n");
                    }
                    excludes[s1] = NULL;
                }
                break;
            case 'e':
                if (getenv("SRS_DOMAIN") != NULL)
                    domain = strdup(getenv("SRS_DOMAIN"));
                if (getenv("SRS_SEPARATOR") != NULL)
                    separator = *getenv("SRS_SEPARATOR");
                if (getenv("SRS_HASHLENGTH") != NULL)
                    hashlength = atoi(getenv("SRS_HASHLENGTH"));
                if (getenv("SRS_HASHMIN") != NULL)
                    hashmin = atoi(getenv("SRS_HASHMIN"));
                if (getenv("SRS_FORWARD_PORT") != NULL)
                    forward_service = strdup(getenv("SRS_FORWARD_PORT"));
                if (getenv("SRS_REVERSE_PORT") != NULL)
                    reverse_service = strdup(getenv("SRS_REVERSE_PORT"));
                if (getenv("SRS_TIMEOUT") != NULL)
                    timeout = atoi(getenv("SRS_TIMEOUT"));
                if (getenv("SRS_SECRET") != NULL)
                    secret_file = strdup(getenv("SRS_SECRET"));
                if (getenv("SRS_PID_FILE") != NULL)
                    pid_file = strdup(getenv("SRS_PID_FILE"));
                if (getenv("RUN_AS") != NULL)
                    user = strdup(getenv("RUN_AS"));
                if (getenv("CHROOT") != NULL)
                    chroot_dir = strdup(getenv("CHROOT"));
                if (getenv("SRS_EXCLUDE_DOMAINS") != NULL)
                {
                    tmp = strtok(getenv("SRS_EXCLUDE_DOMAINS"), ",; \t\r\n");
                    while (tmp)
                    {
                        if (s1 + 1 >= s2)
                        {
                            s2 *= 2;
                            excludes = (const char **)realloc(
                                excludes, s2 * sizeof(char *));
                            if (excludes == NULL)
                            {
                                fprintf(stderr, "%s: Out of memory\n\n", self);
                                return EXIT_FAILURE;
                            }
                        }
                        excludes[s1++] = strdup(tmp);
                        tmp = strtok(NULL, ",; \t\r\n");
                    }
                    excludes[s1] = NULL;
                }
                break;
            case 'v':
                fprintf(stdout, "%s\n", POSTSRSD_VERSION);
                return EXIT_SUCCESS;
        }
    }
    if (optind < argc)
    {
        fprintf(stderr, "%s: extra argument on command line: %s\n", self,
                argv[optind]);
        return EXIT_FAILURE;
    }
    if (domain == NULL || *domain == 0)
    {
        fprintf(stderr, "%s: You must set a home domain (-d)\n", self);
        return EXIT_FAILURE;
    }

    if (separator != '=' && separator != '+' && separator != '-')
    {
        fprintf(stderr, "%s: SRS separator character must be one of '=+-'\n",
                self);
        return EXIT_FAILURE;
    }
    if (forward_service == NULL)
        forward_service = strdup("10001");
    if (reverse_service == NULL)
        reverse_service = strdup("10002");

    /* Close all file descriptors (std ones will be closed later). */
    maxfd = sysconf(_SC_OPEN_MAX);
    for (fd = 3; fd < maxfd; fd++)
        close(fd);

    /* The stuff we do first may not be possible from within chroot or without
     * privileges */

    /* Open pid file for writing (the actual process ID is filled in later) */
    if (pid_file)
    {
        pf = fopen(pid_file, "w");
        if (pf == NULL)
        {
            fprintf(stderr, "%s: Cannot write PID: %s\n\n", self, pid_file);
            return EXIT_FAILURE;
        }
    }
    /* Read secret. The default installation makes this root accessible only. */
    if (secret_file != NULL)
    {
        sf = fopen(secret_file, "rb");
        if (sf == NULL)
        {
            fprintf(stderr, "%s: Cannot open file with secret: %s\n", self,
                    secret_file);
            return EXIT_FAILURE;
        }
    }
    else
    {
        fprintf(stderr, "%s: You must set a secret (-s)\n", self);
        return EXIT_FAILURE;
    }
    /* Bind ports. May require privileges if the config specifies ports below
     * 1024 */
    sc = bind_service(listen_addr, forward_service, family,
                      &sockets[socket_count], 4 - socket_count);
    if (sc == 0)
        return EXIT_FAILURE;
    while (sc-- > 0)
        handler[socket_count++] = handle_forward;
    free(forward_service);
    sc = bind_service(listen_addr, reverse_service, family,
                      &sockets[socket_count], 4 - socket_count);
    if (sc == 0)
        return EXIT_FAILURE;
    while (sc-- > 0)
        handler[socket_count++] = handle_reverse;
    free(reverse_service);
    /* Open syslog now (NDELAY), because it may no longer be reachable from
     * chroot */
    openlog(self, LOG_PID | LOG_NDELAY, LOG_MAIL);
    /* Force loading of timezone info (suggested by patrickdk77) */
    now = time(NULL);
    localtime(&now);
    /* We also have to lookup the uid of the unprivileged user before the
     * chroot. */
    if (user)
    {
        errno = 0;
        pwd = getpwnam(user);
        if (pwd == NULL)
        {
            if (errno != 0)
                fprintf(stderr, "%s: Failed to lookup user: %s\n", self,
                        strerror(errno));
            else
                fprintf(stderr, "%s: No such user: %s\n", self, user);
            return EXIT_FAILURE;
        }
    }
    /* Now we can chroot, which again requires root privileges */
    if (chroot_dir)
    {
        if (chdir(chroot_dir) < 0)
        {
            fprintf(stderr, "%s: Cannot change to chroot: %s\n", self,
                    strerror(errno));
            return EXIT_FAILURE;
        }
        if (chroot(chroot_dir) < 0)
        {
            fprintf(stderr, "%s: Failed to enable chroot: %s\n", self,
                    strerror(errno));
            return EXIT_FAILURE;
        }
    }
    /* Finally, we revert to the unprivileged user */
    if (pwd)
    {
        if (setgid(pwd->pw_gid) < 0)
        {
            fprintf(stderr, "%s: Failed to switch group id: %s\n", self,
                    strerror(errno));
            return EXIT_FAILURE;
        }
        if (setuid(pwd->pw_uid) < 0)
        {
            fprintf(stderr, "%s: Failed to switch user id: %s\n", self,
                    strerror(errno));
            return EXIT_FAILURE;
        }
    }
    /* Standard double fork technique to disavow all knowledge about the
     * controlling terminal */
    if (daemonize)
    {
        close(0);
        close(1);
        close(2);
        if (fork() != 0)
            return EXIT_SUCCESS;
        setsid();
        if (fork() != 0)
            return EXIT_SUCCESS;
    }
    /* Make note of our actual process ID */
    if (pf)
    {
        fprintf(pf, "%d", (int)getpid());
        fclose(pf);
    }

    srs = srs_new();
    while ((secret = fgets(secretbuf, sizeof(secretbuf), sf)))
    {
        secret = strtok(secret, "\r\n");
        if (secret)
            srs_add_secret(srs, secret);
    }
    fclose(sf);

    srs_set_alwaysrewrite(srs, always_rewrite);
    srs_set_separator(srs, separator);
    if (hashlength)
        srs_set_hashlength(srs, hashlength);
    if (hashmin)
        srs_set_hashmin(srs, hashmin);

    if (milter_service != NULL)
    {
        /* Fork and pass child control to the milter library */
        if (fork() == 0)
        {
            /* close listen sockets so that we don't stop the main
             * daemon process from restarting */
            for (int i = 0; i < socket_count; ++i)
                close(sockets[i]);

            return milter_main(milter_service, srs, domain, excludes);
        }
    }

    for (sc = 0; sc < socket_count; ++sc)
    {
        fds[sc].fd = sockets[sc];
        fds[sc].events = POLLIN;
    }
    while (TRUE)
    {
        int conn, flags;
        FILE *fp_read, *fp_write;
        char linebuf[1024], *line;
        char keybuf[1024], *key;

        if (poll(fds, socket_count, 1000) < 0)
        {
            if (errno == EINTR)
                continue;
            if (daemonize)
                syslog(LOG_MAIL | LOG_ERR, "Poll failure: %s", strerror(errno));
            else
                fprintf(stderr, "%s: Poll failure: %s\n", self,
                        strerror(errno));
            return EXIT_FAILURE;
        }
        for (sc = 0; sc < socket_count; ++sc)
        {
            if (fds[sc].revents)
            {
                conn = accept(fds[sc].fd, NULL, NULL);
                if (conn < 0)
                    continue;
                /* remove the nonblocking flag for OSes that bequeath it */
                flags = fcntl(conn, F_GETFL, 0);
                if (flags < 0)
                {
                    close(conn);
                    continue;
                }
                if (fcntl(conn, F_SETFL, flags & ~O_NONBLOCK) < 0)
                {
                    close(conn);
                    continue;
                }
                if (fork() == 0)
                {
                    int i;
                    /* close listen sockets so that we don't stop the main
                     * daemon process from restarting */
                    for (i = 0; i < socket_count; ++i)
                        close(sockets[i]);
                    /* create separate input/output streams */
                    fp_read = fdopen(conn, "r");
                    if (fp_read == NULL)
                        return EXIT_FAILURE;
                    fp_write = fdopen(dup(conn), "w");
                    if (fp_write == NULL)
                        return EXIT_FAILURE;
                    errno = 0;
                    alarm(timeout);
                    if (errno != 0)
                        return EXIT_FAILURE;
                    while ((line = fgets(linebuf, sizeof(linebuf), fp_read)))
                    {
                        char *token;
                        alarm(0);
                        if (strlen(line) >= sizeof(linebuf) - 1)
                        {
                            fprintf(fp_write, "500 Invalid request\n");
                            fflush(fp_write);
                            return EXIT_FAILURE;
                        }
                        token = strtok(line, " \r\n");
                        if (token == NULL || strcmp(token, "get") != 0)
                        {
                            fprintf(fp_write, "500 Invalid request\n");
                            fflush(fp_write);
                            return EXIT_FAILURE;
                        }
                        token = strtok(NULL, "\r\n");
                        if (!token)
                        {
                            fprintf(fp_write, "500 Invalid request\n");
                            fflush(fp_write);
                            return EXIT_FAILURE;
                        }
                        key = url_decode(keybuf, sizeof(keybuf), token);
                        if (!key)
                        {
                            fprintf(fp_write, "500 Invalid request\n");
                            fflush(fp_write);
                            return EXIT_FAILURE;
                        }
                        handler[sc](srs, fp_write, key, domain, excludes);
                        fflush(fp_write);
                        errno = 0;
                        alarm(timeout);
                        if (errno != 0)
                            return EXIT_FAILURE;
                    }
                    fclose(fp_write);
                    fclose(fp_read);
                    return EXIT_SUCCESS;
                }
                close(conn);
            }
        }
        waitpid(-1, NULL, WNOHANG);
    }
    return EXIT_SUCCESS;
}
