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
#ifdef WITH_REDIS
#    include <hiredis.h>
#endif
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
    bool (*write)(struct db_conn*, const char*, const char*, unsigned);
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
    int result = sqlite3_step(conn->read_stmt);
    if (result == SQLITE_ERROR)
    {
        sqlite3* handle = (sqlite3*)conn->handle;
        log_warn("sqlite read error: %s", sqlite3_errmsg(handle));
    }
    if (result == SQLITE_ROW)
    {
        value = strdup((const char*)sqlite3_column_text(conn->read_stmt, 0));
    }
    sqlite3_reset(conn->read_stmt);
    sqlite3_clear_bindings(conn->read_stmt);
    return value;
}

static bool db_sqlite_write(struct db_conn* conn, const char* key,
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
            return false;
        }
    }
    bool success = true;
    sqlite3_bind_text(conn->write_stmt, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_text(conn->write_stmt, 2, value, -1, SQLITE_STATIC);
    sqlite3_bind_int64(conn->write_stmt, 3, time(NULL) + lifetime);
    if (sqlite3_step(conn->write_stmt) == SQLITE_ERROR)
    {
        sqlite3* handle = (sqlite3*)conn->handle;
        log_warn("sqlite write error: %s", sqlite3_errmsg(handle));
        success = false;
    }
    sqlite3_reset(conn->write_stmt);
    sqlite3_clear_bindings(conn->write_stmt);
    return success;
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

static bool db_sqlite_connect(struct db_conn* conn, const char* uri,
                              bool create_if_not_exist)
{
    sqlite3* handle;
    char* err;
    if (sqlite3_open(uri, &handle) != SQLITE_OK)
    {
        sqlite3_close(handle);
        return false;
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
            return false;
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
    return true;
}
#endif

#ifdef WITH_REDIS
static char* db_redis_read(struct db_conn* conn, const char* key)
{
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "PostSRSd/%s", key);
    redisContext* handle = (redisContext*)conn->handle;
    redisReply* reply = redisCommand(handle, "GET %s", buffer);
    char* value = NULL;
    if (reply->type == REDIS_REPLY_ERROR)
    {
        log_warn("redis read error: %s", reply->str);
    }
    if (reply->type == REDIS_REPLY_STRING)
    {
        value = strdup(reply->str);
    }
    freeReplyObject(reply);
    return value;
}

static bool db_redis_write(struct db_conn* conn, const char* key,
                           const char* value, unsigned lifetime)
{
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "PostSRSd/%s", key);
    redisContext* handle = (redisContext*)conn->handle;
    bool success = true;
    redisReply* reply =
        redisCommand(handle, "SETEX %s %u %s", buffer, lifetime, value);
    if (reply->type == REDIS_REPLY_ERROR)
    {
        log_warn("redis write error: %s", reply->str);
        success = false;
    }
    freeReplyObject(reply);
    return success;
}

static void db_redis_disconnect(struct db_conn* conn)
{
    redisContext* handle = (redisContext*)conn->handle;
    redisFree(handle);
}

static bool db_redis_connect(struct db_conn* conn, const char* hostname,
                             int port)
{
    redisContext* handle;
    if (port > 0)
    {
        handle = redisConnect(hostname, port);
    }
    else
    {
        handle = redisConnectUnix(hostname);
    }
    if (!handle)
    {
        log_error("failed to allocate redis handle");
        return false;
    }
    if (handle->err)
    {
        log_error("failed to connect to redis instance: %s", handle->errstr);
        redisFree(handle);
        return false;
    }
    redisEnableKeepAlive(handle);
    conn->handle = handle;
    conn->read = db_redis_read;
    conn->write = db_redis_write;
    conn->expire = NULL;
    conn->disconnect = db_redis_disconnect;
    return true;
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
            log_error("failed to allocate database connection handle");
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
#ifdef WITH_REDIS
    if (strncmp(uri, "redis:", 6) == 0)
    {
        struct db_conn* conn = (struct db_conn*)malloc(sizeof(struct db_conn));
        if (conn == NULL)
        {
            log_error("failed to allocate database connection handle");
            return NULL;
        }
        int port;
        char* hostname = endpoint_for_redis(&uri[6], &port);
        if (hostname == NULL)
        {
            log_error("invalid database uri '%s'", uri);
            free(conn);
            return NULL;
        }
        if (!db_redis_connect(conn, hostname, port))
        {
            log_error("failed to connect to '%s'", uri);
            free(hostname);
            free(conn);
            return NULL;
        }
        free(hostname);
        return conn;
    }
#endif
    log_error("unsupported database '%s'", uri);
    return NULL;
}

char* database_read(struct db_conn* conn, const char* key)
{
    if (conn && key)
        return conn->read(conn, key);
    return NULL;
}

bool database_write(struct db_conn* conn, const char* key, const char* value,
                    unsigned lifetime)
{
    if (conn && key && value)
        return conn->write(conn, key, value, lifetime);
    return false;
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
