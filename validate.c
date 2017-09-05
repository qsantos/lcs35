#include "session.h"
#include "util.h"

// external libraries
#include <sqlite3.h>

// C99
#include <inttypes.h>

// C90
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// POSIX
#include <pthread.h>

static void validate_session(const struct session* session,
                      const struct session* previous) {
    // redo the work from previous session to session to validate
    struct session* redo = session_copy(previous);
    redo->t = session->i; // stop when session to validate is reached
    uint64_t work_todo = redo->t - previous->i;
    while (session_work(redo, 1ull<<20)) {
        uint64_t work_done = redo->i - previous->i;
        double progress = (double) work_done / (double) work_todo;
        printf("%#.12" PRIx64 " -> %#.12" PRIx64 ": %5.1f%%\n",
               previous->i, session->i, 100*progress);
    }
    redo->t = session->t; // restore target exponent

    // compare new result to session to validate
    if (redo->i != session->i) {
        // that should not happen
        LOG(FATAL, "mismatched exponents; this is most peculiar");
        exit(EXIT_FAILURE);
    }
    if (mpz_cmp(redo->w, session->w) != 0) {
        LOG(ERR, "INVALID %#.12" PRIx64 " -> %#.12" PRIx64, previous->i,
            session->i);
    }

    // clean up
    session_delete(redo);
}

struct checkpoints_queue {
    pthread_mutex_t lock;
    /* query for all checkpoints in increasing order of i */
    sqlite3_stmt* stmt_checkkpoints;
    /* values for i and w before the checkpoint returned by next call to
     * sqlite3_step(stmt_checkkpoints) */
    uint64_t last_i;
    mpz_t last_w;
};

static void* worker(void* argument) {
    struct checkpoints_queue* queue = argument;

    /* checks will be done by running session_validate() from session_a to
     * session_b */
    struct session* session_a = session_new();
    struct session* session_b = session_new();

    while (1) {
        pthread_mutex_lock(&queue->lock);
        if (sqlite3_step(queue->stmt_checkkpoints) != SQLITE_ROW) {
            break;
        }

        // use parameters from current row for session_b
        session_b->i = (uint64_t) sqlite3_column_int64(queue->stmt_checkkpoints, 0);
        const char* str_w = (const char*) sqlite3_column_text(queue->stmt_checkkpoints, 1);
        if (mpz_set_str(session_b->w, str_w, 10) < 0) {
            LOG(FATAL, "invalid decimal number w = %s", str_w);
            exit(EXIT_FAILURE);
        }

        // use last parameters from queue for session_a
        session_a->i = queue->last_i;
        mpz_set(session_a->w, queue->last_w);

        // before releasing the lock, update queue information
        queue->last_i = session_b->i;
        mpz_set(queue->last_w, session_b->w);

        pthread_mutex_unlock(&queue->lock);

        // complete the task
        validate_session(session_b, session_a);
    }

    session_delete(session_b);
    session_delete(session_a);
    return NULL;
}

extern int main(int argc, char** argv) {
    // pre-parse arguments
    parse_debug_args(&argc, argv);

    // allocate array of session pointers
    if (argc != 2) {
        LOG(FATAL, "usage: %s savefile.db", argv[0]);
        exit(EXIT_FAILURE);
    }

    // make sure sqlite3 is thread-safe
    if (!sqlite3_threadsafe()) {
        LOG(FATAL, "SQLite is not threadsafe");
        exit(EXIT_FAILURE);
    }

    // open sqlite3 database
    sqlite3* db;
    if (sqlite3_open(argv[1], &db) != SQLITE_OK) {
        LOG(FATAL, "sqlite3_open: %s", sqlite3_errmsg(db));
        exit(EXIT_FAILURE);
    }

    struct checkpoints_queue queue = {
        .last_i = 0,
    };
    mpz_init_set_ui(queue.last_w, 2);
    pthread_mutex_init(&queue.lock, NULL);

    if (sqlite3_prepare_v2(db, "SELECT i, w FROM checkpoint ORDER BY i", -1,
                           &queue.stmt_checkkpoints, NULL) != SQLITE_OK) {
        LOG(FATAL, "sqlite3_prepare_v2: %s", sqlite3_errmsg(db));
        exit(EXIT_FAILURE);
    }

#define N_THREADS 4
    pthread_t threads[N_THREADS];
    for (size_t i = 0; i < N_THREADS; i += 1) {
        int ret = pthread_create(&threads[i], NULL, worker, &queue);
        if (ret != 0) {
            LOG(FATAL, "failed to start thread (%s)", strerror(ret));
            exit(EXIT_FAILURE);
        }
    }

    puts("Working...");

    for (size_t i = 0; i < N_THREADS; i += 1) {
        int ret = pthread_join(threads[i], NULL);
        if (ret != 0) {
            LOG(ERR, "failed to join thread (%s)", strerror(ret));
        }
    }

    // clean up
    sqlite3_finalize(queue.stmt_checkkpoints);
    mpz_clear(queue.last_w);
    pthread_mutex_destroy(&queue.lock);
    return EXIT_SUCCESS;
}
