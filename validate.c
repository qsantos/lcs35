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

struct checkpoints_queue {
    pthread_mutex_t lock;
    /* database handle */
    sqlite3* db;
    /* query for all checkpoints in increasing order of i */
    sqlite3_stmt* stmt_checkkpoints;
    /* values for i and w before the checkpoint returned by next call to
     * sqlite3_step(stmt_checkkpoints) */
    uint64_t last_i;
    mpz_t last_w;
};

static void* worker(void* argument) {
    struct checkpoints_queue* queue = argument;

    /* session used to redo the computations */
    struct session* session = session_new();
    mpz_t next_w;
    mpz_init(next_w);

    while (1) {
        pthread_mutex_lock(&queue->lock);
        if (sqlite3_step(queue->stmt_checkkpoints) != SQLITE_ROW) {
            pthread_mutex_unlock(&queue->lock);
            break;
        }

        uint64_t next_i = (uint64_t) sqlite3_column_int64(queue->stmt_checkkpoints, 0);
        const char* str_w = (const char*) sqlite3_column_text(queue->stmt_checkkpoints, 1);
        if (mpz_set_str(next_w, str_w, 10) < 0) {
            LOG(FATAL, "invalid decimal number w = %s", str_w);
            pthread_mutex_unlock(&queue->lock);
            exit(EXIT_FAILURE);
        }

        // use last parameters from queue for session
        uint64_t last_i = queue->last_i;  // save it to compute progress
        session->i = last_i;
        mpz_set(session->w, queue->last_w);

        // before releasing the lock, update queue information
        queue->last_i = next_i;
        mpz_set(queue->last_w, next_w);

        pthread_mutex_unlock(&queue->lock);

#define MIN(a, b) ((a) < (b) ? (a) : (b))

        // work from previous checkpoint; make new ones every regularly
        session->t = ((session->i >> 25) + 1) << 25;  // next multiple of 2**25
        while (session->t < next_i) {

            while (session_work(session, 1ull<<20)) {
                double progress = (double) (session->i - last_i) / (double) (next_i - last_i);
                printf("%#.12" PRIx64 " -> %#.12" PRIx64 ": %5.1f%%\n",
                       last_i, next_i, 100*progress);
            }
            session_checkpoint_insert(session, queue->db);
            session->t += 1 << 25;
        }

        // complete checking up to next checkpoint
        session->t = next_i;
        while (session_work(session, 1ull<<20)) {
            double progress = (double) (session->i - last_i) / (double) (next_i - last_i);
            printf("%#.12" PRIx64 " -> %#.12" PRIx64 ": %5.1f%%\n",
                   last_i, next_i, 100*progress);
        }
        session_checkpoint_update(session, queue->db);

        if (mpz_cmp(session->w, next_w) != 0) {
            LOG(ERR, "INVALID %#.12" PRIx64 " -> %#.12" PRIx64, last_i,
                session->i);
        }
    }

    session_delete(session);
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

    // make sure sqlite3 is serialized
    if (!sqlite3_threadsafe()) {
        LOG(FATAL, "SQLite is not threadsafe");
        exit(EXIT_FAILURE);
    }
    sqlite3_config(SQLITE_CONFIG_SERIALIZED);

    struct checkpoints_queue queue = {
        .last_i = 0,
    };
    mpz_init_set_ui(queue.last_w, 2);
    pthread_mutex_init(&queue.lock, NULL);

    // open sqlite3 database
    if (sqlite3_open(argv[1], &queue.db) != SQLITE_OK) {
        LOG(FATAL, "sqlite3_open: %s", sqlite3_errmsg(queue.db));
        exit(EXIT_FAILURE);
    }

    if (sqlite3_prepare_v2(queue.db, "SELECT i, w FROM checkpoint ORDER BY i", -1,
                           &queue.stmt_checkkpoints, NULL) != SQLITE_OK) {
        LOG(FATAL, "sqlite3_prepare_v2: %s", sqlite3_errmsg(queue.db));
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
