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

struct database
{
    char* (*read)(database_t*, const char*);
    bool (*write)(database_t*, const char*, const char*, unsigned);
    void (*expire)(database_t*);
    void (*disconnect)(database_t*);
    void* handle;
#ifdef WITH_SQLITE
    sqlite3_stmt *read_stmt, *write_stmt, *expire_stmt;
#endif
};

#ifdef WITH_SQLITE
static char* db_sqlite_read(database_t* db, const char* key)
{
    if (db->read_stmt == NULL)
    {
        sqlite3* handle = (sqlite3*)db->handle;
        if (sqlite3_prepare_v2(handle, "SELECT v FROM kv WHERE k = ?", -1,
                               &db->read_stmt, NULL)
            != SQLITE_OK)
        {
            log_error("failed to prepare sqlite read statement");
            return NULL;
        }
    }
    char* value = NULL;
    sqlite3_bind_text(db->read_stmt, 1, key, -1, SQLITE_STATIC);
    int result = sqlite3_step(db->read_stmt);
    if (result == SQLITE_ERROR)
    {
        sqlite3* handle = (sqlite3*)db->handle;
        log_warn("sqlite read error: %s", sqlite3_errmsg(handle));
    }
    if (result == SQLITE_ROW)
    {
        value = strdup((const char*)sqlite3_column_text(db->read_stmt, 0));
    }
    sqlite3_reset(db->read_stmt);
    sqlite3_clear_bindings(db->read_stmt);
    return value;
}

static bool db_sqlite_write(database_t* db, const char* key, const char* value,
                            unsigned lifetime)
{
    if (db->write_stmt == NULL)
    {
        sqlite3* handle = (sqlite3*)db->handle;
        if (sqlite3_prepare_v2(handle,
                               "INSERT INTO kv (k, v, lt) VALUES (?, ?, ?)", -1,
                               &db->write_stmt, NULL)
            != SQLITE_OK)
        {
            log_error("failed to prepare sqlite write statement");
            return false;
        }
    }
    bool success = true;
    sqlite3_bind_text(db->write_stmt, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_text(db->write_stmt, 2, value, -1, SQLITE_STATIC);
    sqlite3_bind_int64(db->write_stmt, 3, time(NULL) + lifetime);
    if (sqlite3_step(db->write_stmt) == SQLITE_ERROR)
    {
        sqlite3* handle = (sqlite3*)db->handle;
        log_warn("sqlite write error: %s", sqlite3_errmsg(handle));
        success = false;
    }
    sqlite3_reset(db->write_stmt);
    sqlite3_clear_bindings(db->write_stmt);
    return success;
}

static void db_sqlite_expire(database_t* db)
{
    if (db->expire_stmt == NULL)
    {
        sqlite3* handle = (sqlite3*)db->handle;
        if (sqlite3_prepare_v2(handle, "DELETE FROM kv WHERE lt <= ?", -1,
                               &db->expire_stmt, NULL)
            != SQLITE_OK)
        {
            log_error("failed to prepare sqlite expire statement");
            return;
        }
    }
    sqlite3_bind_int64(db->expire_stmt, 1, time(NULL));
    sqlite3_step(db->expire_stmt);
    sqlite3_reset(db->expire_stmt);
}

static void db_sqlite_disconnect(database_t* db)
{
    sqlite3* handle = (sqlite3*)db->handle;
    sqlite3_finalize(db->expire_stmt);
    sqlite3_finalize(db->read_stmt);
    sqlite3_finalize(db->write_stmt);
    sqlite3_close(handle);
}

static bool db_sqlite_connect(database_t* db, const char* uri,
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
    db->handle = handle;
    db->read = db_sqlite_read;
    db->write = db_sqlite_write;
    db->expire = db_sqlite_expire;
    db->disconnect = db_sqlite_disconnect;
    db->read_stmt = NULL;
    db->write_stmt = NULL;
    db->expire_stmt = NULL;
    return true;
}
#endif

#ifdef WITH_REDIS
static char* db_redis_read(database_t* db, const char* key)
{
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "PostSRSd/%s", key);
    redisContext* handle = (redisContext*)db->handle;
    redisReply* reply = redisCommand(handle, "GET %s", buffer);
    if (reply == NULL)
    {
        log_warn("redis connection failure: %s", handle->errstr);
        return NULL;
    }
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

static bool db_redis_write(database_t* db, const char* key, const char* value,
                           unsigned lifetime)
{
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "PostSRSd/%s", key);
    redisContext* handle = (redisContext*)db->handle;
    bool success = true;
    redisReply* reply =
        redisCommand(handle, "SETEX %s %u %s", buffer, lifetime, value);
    if (reply == NULL)
    {
        log_warn("redis connection failure: %s", handle->errstr);
        return false;
    }
    if (reply->type == REDIS_REPLY_ERROR)
    {
        log_warn("redis write error: %s", reply->str);
        success = false;
    }
    freeReplyObject(reply);
    return success;
}

static void db_redis_disconnect(database_t* db)
{
    redisContext* handle = (redisContext*)db->handle;
    redisFree(handle);
}

static bool db_redis_connect(database_t* db, const char* hostname, int port)
{
    redisContext* handle;
    if (port > 0)
    {
        handle = redisConnect(hostname, port);
        if (handle == NULL)
            goto alloc_fail;
        if (handle->err)
            goto conn_fail;
        redisEnableKeepAlive(handle);
    }
    else
    {
        handle = redisConnectUnix(hostname);
        if (handle == NULL)
            goto alloc_fail;
    }
    if (handle->err)
        goto conn_fail;
    db->handle = handle;
    db->read = db_redis_read;
    db->write = db_redis_write;
    db->expire = NULL;
    db->disconnect = db_redis_disconnect;
    return true;

conn_fail:
    log_error("failed to connect to redis instance: %s", handle->errstr);
    redisFree(handle);
    return false;

alloc_fail:
    log_error("failed to allocate redis handle");
    return false;
}
#endif

database_t* database_connect(const char* uri, bool create_if_not_exist)
{
    MAYBE_UNUSED(create_if_not_exist);
    if (uri == NULL || *uri == 0)
    {
        log_error("not database uri configured");
        return NULL;
    }
#ifdef WITH_SQLITE
    if (strncmp(uri, "sqlite:", 7) == 0)
    {
        database_t* db = (database_t*)malloc(sizeof(struct database));
        if (db == NULL)
        {
            log_error("failed to allocate database connection handle");
            return NULL;
        }
        if (!db_sqlite_connect(db, uri + 7, create_if_not_exist))
        {
            log_error("failed to connect to '%s'", uri);
            free(db);
            return NULL;
        }
        return db;
    }
#endif
#ifdef WITH_REDIS
    if (strncmp(uri, "redis:", 6) == 0)
    {
        database_t* db = (database_t*)malloc(sizeof(struct database));
        if (db == NULL)
        {
            log_error("failed to allocate database connection handle");
            return NULL;
        }
        int port;
        char* hostname = endpoint_for_redis(&uri[6], &port);
        if (hostname == NULL)
        {
            log_error("invalid database uri '%s'", uri);
            free(db);
            return NULL;
        }
        if (!db_redis_connect(db, hostname, port))
        {
            log_error("failed to connect to '%s'", uri);
            free(hostname);
            free(db);
            return NULL;
        }
        free(hostname);
        return db;
    }
#endif
    log_error("unsupported database '%s'", uri);
    return NULL;
}

char* database_read(database_t* db, const char* key)
{
    if (db != NULL && key != NULL)
        return db->read(db, key);
    return NULL;
}

bool database_write(database_t* db, const char* key, const char* value,
                    unsigned lifetime)
{
    if (db != NULL && key != NULL && value != NULL)
        return db->write(db, key, value, lifetime);
    return false;
}

void database_expire(database_t* db)
{
    if (db != NULL && db->expire != NULL)
        db->expire(db);
}

void database_disconnect(database_t* db)
{
    if (db != NULL)
        db->disconnect(db);
    free(db);
}
