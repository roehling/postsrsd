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
#include "util.h"

#include "postsrsd_build_config.h"

#include <stdlib.h>
#include <string.h>
#ifdef HAVE_SYS_FILE_H
#    include <sys/file.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#    include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
#    include <sys/stat.h>
#endif
#include <unistd.h>

void set_string(char** var, char* value)
{
    free(*var);
    *var = value;
}

int file_exists(const char* filename)
{
    struct stat st;
    if (stat(filename, &st) < 0)
        return 0;
    return S_ISREG(st.st_mode);
}

int directory_exists(const char* dirname)
{
    struct stat st;
    if (stat(dirname, &st) < 0)
        return 0;
    return S_ISDIR(st.st_mode);
}

int acquire_lock(const char* path)
{
#if defined(LOCK_EX) && defined(LOCK_NB)
    size_t len = strlen(path);
    char* lock_path = malloc(len + 6); /* ".lock" + "\0" */
    strcpy(lock_path, path);
    strcat(lock_path, ".lock");
    int fd = open(lock_path, O_RDONLY | O_CREAT, 0600);
    free(lock_path);
    if (fd < 0)
        return -1;
    if (flock(fd, LOCK_EX | LOCK_NB) < 0)
    {
        close(fd);
        return -1;
    }
    return fd;
#else
    return 0;
#endif
}

void release_lock(const char* path, int fd)
{
#if defined(LOCK_EX) && defined(LOCK_NB)
    size_t len = strlen(path);
    char* lock_path = malloc(len + 6); /* ".lock" + "\0" */
    strcpy(lock_path, path);
    strcat(lock_path, ".lock");
    unlink(lock_path);
    free(lock_path);
    close(fd);
#endif
}

struct domain_set
{
    struct domain_set* c[37];
    struct domain_set* s;
    int m;
};

#define DOMAIN_SET_ADD      1
#define DOMAIN_SET_CONTAINS 0
#define DOMAIN_SET_REMOVE   -1

struct domain_set* domain_set_create()
{
    struct domain_set* D = malloc(sizeof(struct domain_set));
    for (unsigned i = 0; i < sizeof(D->c) / sizeof(D->c[0]); ++i)
        D->c[i] = NULL;
    D->s = NULL;
    D->m = 0;
    return D;
}

void domain_set_destroy(struct domain_set* D)
{
    for (unsigned i = 0; i < sizeof(D->c) / sizeof(D->c[0]); ++i)
        if (D->c[i])
            domain_set_destroy(D->c[i]);
    if (D->s)
        domain_set_destroy(D->s);
    free(D);
}

int walk_domain_set(struct domain_set* D, char* domain, int flag)
{
    char* dot = strrchr(domain, '.');
    char* subdomain = domain;
    if (dot)
    {
        subdomain = dot + 1;
        *dot = 0;
    }
    int ch;
    while ((ch = *subdomain++))
    {
        if (ch >= 'A' && ch <= 'Z')
            ch -= 'A';
        else if (ch >= 'a' && ch <= 'z')
            ch -= 'a';
        else if (ch >= '0' && ch <= '9')
            ch = ch - '0' + 26;
        else if (ch == '-')
            ch = 36;
        else
            return 0;
        if (D->c[ch] == NULL)
        {
            if (flag != DOMAIN_SET_ADD)
                return 0;
            D->c[ch] = domain_set_create();
        }
        D = D->c[ch];
    }
    if (dot)
    {
        if (D->s == NULL)
        {
            if (flag != DOMAIN_SET_ADD)
                return 0;
            D->s = domain_set_create();
        }
        return walk_domain_set(D->s, domain, flag);
    }
    int result = D->m;
    if (flag == DOMAIN_SET_ADD)
        D->m = 1;
    if (flag == DOMAIN_SET_REMOVE)
        D->m = 0;
    return result;
}

int domain_set_add(struct domain_set* D, const char* domain)
{
    char buffer[1024];
    strncpy(buffer, domain, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = 0;
    return !walk_domain_set(D, buffer, DOMAIN_SET_ADD);
}

int domain_set_contains(struct domain_set* D, const char* domain)
{
    char buffer[1024];
    strncpy(buffer, domain, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = 0;
    return walk_domain_set(D, buffer, DOMAIN_SET_CONTAINS);
}
