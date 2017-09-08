#include "session.h"
#include "socket.h"
#include "util.h"

// external libraries
#include <sqlite3.h>

// POSIX
#include <unistd.h>

// C99
#include <inttypes.h>

// C90
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int startswith(const char* string, const char* prefix) {
    while (*prefix != '\0') {
        if (*string != *prefix) {
            return 0;
        }
        prefix += 1;
        string += 1;
    }
    return 1;
}

static int db_get_last_i_w(sqlite3* db, uint64_t* i, const char** w) {
    // load last checkpoint
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(
            db, "SELECT i, w FROM checkpoint ORDER BY i DESC LIMIT 1", -1,
            &stmt, NULL) != SQLITE_OK) {
        LOG(WARN, "sqlite3_prepare_v2: %s", sqlite3_errmsg(db));
        return -1;
    }
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        // database empty, start from scratch
        *i = 0;
        *w = "2";
        LOG(DEBUG, "Database empty");
        return 0;
    }
    *i = (uint64_t) sqlite3_column_int64(stmt, 0);
    *w = (const char*) sqlite3_column_text(stmt, 1);
    if (sqlite3_finalize(stmt) != SQLITE_OK) {
        LOG(WARN, "sqlite3_finalize: %s", sqlite3_errmsg(db));
        return -1;
    }
    return 0;
}

extern int db_append_i_w(sqlite3* db, uint64_t i, const char* w) {
    /* Create a new checkpoint; values were computed for the first time */

    // TODO
    // quick ugly check of consistency
    struct session* session = session_new();
    session->i = i;
    if (mpz_set_str(session->w, w, 0) < 0) {
        LOG(WARN, "failed to parse w");
        return -1;
    }
    if (session_check(session) != 0) {
        LOG(WARN, "invalid (i, w) pair");
        return -1;
    }

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(
            db, "INSERT OR IGNORE INTO checkpoint (i, w) VALUES (?, ?)",
            -1, &stmt, NULL
    ) != SQLITE_OK) {
        LOG(WARN, "sqlite3_prepare_v2: %s", sqlite3_errmsg(db));
        return -1;
    }
    if (sqlite3_bind_int64(stmt, 1, (sqlite_int64) i) != SQLITE_OK) {
        LOG(WARN, "sqlite3_bind_text: %s", sqlite3_errmsg(db));
        return -1;
    }
    if (sqlite3_bind_text(stmt, 2, w, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        LOG(WARN, "sqlite3_bind_text: %s", sqlite3_errmsg(db));
        return -1;
    }
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        LOG(WARN, "sqlite3_step: %s", sqlite3_errmsg(db));
        return -1;
    }
    if (sqlite3_finalize(stmt) != SQLITE_OK) {
        LOG(WARN, "sqlite3_finalize: %s", sqlite3_errmsg(db));
        return -1;
    }

    return 0;
}

static int handle_client(int client, sqlite3* db) {
    char buffer[1024];
    ssize_t n = read(client, buffer, sizeof(buffer) - 1);
    if (n < 0) {
        LOG(ERR, "failed to read socket");
        return -1;
    }
    buffer[n] = 0;
    LOG(DEBUG, "buffer: <%s>", buffer);
    if (startswith(buffer, "resume:")) {
        LOG(INFO, "command: resume");
        const char* w = "";
        uint64_t i = 0;
        if (db_get_last_i_w(db, &i, &w) < 0) {
            LOG(ERR, "failed to obtain last i and w");
            return -1;
        }
        n = snprintf(buffer, sizeof(buffer), "%#"PRIx64":%s", i, w);
        if (n < 0) {
            LOG(ERR, "failed to create message");
            return -1;
        }
        write(client, buffer, (size_t) n);
    } else if (startswith(buffer, "save:")) {
        LOG(INFO, "command: save");
        char* w;
        uint64_t i = strtoul(&buffer[5], &w, 0);
        if (*w != ':') {
            LOG(ERR, "i in save command not followed by colon");
            return -1;
        }
        w += 1;
        if (db_append_i_w(db, i, w) < 0) {
            LOG(ERR, "i and w were not properly saved");
            return -1;
        }
        LOG(DEBUG, "saved i = %#"PRIx64", w = %s", i, w);
    } else if (startswith(buffer, "mandate:")) {
        LOG(INFO, "command: mandate");
        // TODO
    } else if (startswith(buffer, "validate:")) {
        LOG(INFO, "command: validate");
        // TODO
    } else {
        LOG(ERR, "unknown command %s", buffer);
        return -1;
    }
    return 0;
}

int main(int argc, char** argv) {
    // pre-parse arguments
    parse_debug_args(&argc, argv);
    if (argc != 2) {
        LOG(FATAL, "usage: %s savefile.db", argv[0]);
        exit(EXIT_FAILURE);
    }

    // open sqlite3 database
    sqlite3* db;
    if (sqlite3_open(argv[1], &db) != SQLITE_OK) {
        LOG(FATAL, "sqlite3_open: %s", sqlite3_errmsg(db));
        exit(EXIT_FAILURE);
    }

    // create table if necessary
    char* errmsg;
    sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS checkpoint ("
        "    i INTEGER UNIQUE,"
        "    w TEXT,"
        "    first_computed TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "    last_computed TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ");", NULL, NULL, &errmsg);
    if (errmsg != NULL) {
        LOG(FATAL, "%s", errmsg);
        return 1;
    }

    const char* port = "4242";
    int listener = tcp_listen(port);
    if (listener < 0) {
        LOG(FATAL, "failed to listen on port %s", port);
        exit(EXIT_FAILURE);
    }
    LOG(INFO, "listening on port %s", port);

    while (1) {
        int client = tcp_accept(listener);
        if (client < 0) {
            LOG(ERR, "failed to open client socket");
            continue;
        }
        LOG(INFO, "connection received");
        handle_client(client, db);
        close(client);
    }
}
