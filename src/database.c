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
#include "database.h"

#include "postsrsd_build_config.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>
#ifdef WITH_SQLITE
#    include <sqlite3.h>
#endif
#ifdef HAVE_SYS_TIME_H
#    include <sys/time.h>
#endif
#ifdef HAVE_TIME_H
#    include <time.h>
#endif

struct db_conn
{
    char* (*read)(struct db_conn*, const char*);
    void (*write)(struct db_conn*, const char*, const char*, unsigned);
    void (*expire)(struct db_conn*);
    void (*disconnect)(struct db_conn*);
    void* handle;
#ifdef WITH_SQLITE
    sqlite3_stmt *read_stmt, *write_stmt, *expire_stmt;
#endif
};

#ifdef WITH_SQLITE
static char* db_sqlite_read(struct db_conn* conn, const char* key)
{
    if (conn->read_stmt == NULL)
    {
        sqlite3* handle = (sqlite3*)conn->handle;
        if (sqlite3_prepare_v2(handle, "SELECT v FROM kv WHERE k = ?", -1,
                               &conn->read_stmt, NULL)
            != SQLITE_OK)
        {
            log_error("failed to prepare sqlite read statement");
            return NULL;
        }
    }
    char* value = NULL;
    sqlite3_bind_text(conn->read_stmt, 1, key, -1, SQLITE_STATIC);
    if (sqlite3_step(conn->read_stmt) == SQLITE_ROW)
    {
        value = strdup((const char*)sqlite3_column_text(conn->read_stmt, 0));
    }
    sqlite3_reset(conn->read_stmt);
    sqlite3_clear_bindings(conn->read_stmt);
    return value;
}

static void db_sqlite_write(struct db_conn* conn, const char* key,
                            const char* value, unsigned lifetime)
{
    if (conn->write_stmt == NULL)
    {
        sqlite3* handle = (sqlite3*)conn->handle;
        if (sqlite3_prepare_v2(handle,
                               "INSERT INTO kv (k, v, lt) VALUES (?, ?, ?)", -1,
                               &conn->write_stmt, NULL)
            != SQLITE_OK)
        {
            log_error("failed to prepare sqlite write statement");
            return;
        }
    }
    sqlite3_bind_text(conn->write_stmt, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_text(conn->write_stmt, 2, value, -1, SQLITE_STATIC);
    sqlite3_bind_int64(conn->write_stmt, 3, time(NULL) + lifetime);
    sqlite3_step(conn->write_stmt);
    sqlite3_reset(conn->write_stmt);
    sqlite3_clear_bindings(conn->write_stmt);
}

static void db_sqlite_expire(struct db_conn* conn)
{
    if (conn->expire_stmt == NULL)
    {
        sqlite3* handle = (sqlite3*)conn->handle;
        if (sqlite3_prepare_v2(handle, "DELETE FROM kv WHERE lt <= ?", -1,
                               &conn->expire_stmt, NULL)
            != SQLITE_OK)
        {
            log_error("failed to prepare sqlite expire statement");
            return;
        }
    }
    sqlite3_bind_int64(conn->expire_stmt, 1, time(NULL));
    sqlite3_step(conn->expire_stmt);
    sqlite3_reset(conn->expire_stmt);
}

static void db_sqlite_disconnect(struct db_conn* conn)
{
    sqlite3* handle = (sqlite3*)conn->handle;
    sqlite3_finalize(conn->expire_stmt);
    sqlite3_finalize(conn->read_stmt);
    sqlite3_finalize(conn->write_stmt);
    sqlite3_close(handle);
}

static int db_sqlite_connect(struct db_conn* conn, const char* uri,
                             bool create_if_not_exist)
{
    sqlite3* handle;
    char* err;
    if (sqlite3_open(uri, &handle) != SQLITE_OK)
    {
        sqlite3_close(handle);
        return 0;
    }
    if (create_if_not_exist)
    {
        if (sqlite3_exec(handle,
                         "CREATE TABLE IF NOT EXISTS kv ("
                         "k TEXT NOT NULL UNIQUE ON CONFLICT REPLACE,"
                         "v TEXT NOT NULL,"
                         "lt INTEGER NOT NULL);"
                         "CREATE INDEX IF NOT EXISTS ltidx ON kv (lt)",
                         NULL, NULL, &err)
            != SQLITE_OK)
        {
            if (err != NULL)
            {
                log_error("%s", err);
                sqlite3_free(err);
            }
            sqlite3_close(handle);
            return 0;
        }
    }
    conn->handle = handle;
    conn->read = db_sqlite_read;
    conn->write = db_sqlite_write;
    conn->expire = db_sqlite_expire;
    conn->disconnect = db_sqlite_disconnect;
    conn->read_stmt = NULL;
    conn->write_stmt = NULL;
    conn->expire_stmt = NULL;
    return 1;
}
#endif

struct db_conn* database_connect(const char* uri, bool create_if_not_exist)
{
#ifdef WITH_SQLITE
    if (strncmp(uri, "sqlite:", 7) == 0)
    {
        struct db_conn* conn = (struct db_conn*)malloc(sizeof(struct db_conn));
        if (conn == NULL)
        {
            log_error("failed to allocated database connection handle");
            return NULL;
        }
        if (!db_sqlite_connect(conn, uri + 7, create_if_not_exist))
        {
            log_error("failed to connect to '%s'", uri);
            free(conn);
            return NULL;
        }
        return conn;
    }
#endif
    log_error("unsupported database '%s'", uri);
    return NULL;
}

char* database_read(struct db_conn* conn, const char* key)
{
    if (conn)
        return conn->read(conn, key);
    return NULL;
}

void database_write(struct db_conn* conn, const char* key, const char* value,
                    unsigned lifetime)
{
    if (conn)
        conn->write(conn, key, value, lifetime);
}

void database_expire(struct db_conn* conn)
{
    if (conn && conn->expire)
        conn->expire(conn);
}

void database_disconnect(struct db_conn* conn)
{
    if (conn)
        conn->disconnect(conn);
    free(conn);
}
