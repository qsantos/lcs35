#include "session.h"
#include "util.h"

// C90
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// POSIX
#include <pthread.h>

static int session_cmp(const void* _a, const void* _b) {
    struct session* const* a = _a;
    struct session* const* b = _b;
    if ((*a)->i > (*b)->i) {
        return 1;
    } else if ((*a)->i == (*b)->i) {
        return 0;
    } else {
        return -1;
    }
}

static void validate_session(const struct session* session,
                      const struct session* previous) {
    // redo the work from previous session to session to validate
    struct session* redo = session_copy(previous);
    redo->t = session->i; // stop when session to validate is reached
    uint64_t work_todo = redo->t - previous->i;
    while (session_work(redo, 1ull<<20)) {
        uint64_t work_done = redo->i - previous->i;
        double progress = (double) work_done / (double) work_todo;
        printf("%s -> %s: %5.1f%%\n", previous->metadata, session->metadata,
               100*progress);
    }
    redo->t = session->t; // restore target exponent

    // compare new result to session to validate
    if (redo->i != session->i) {
        // that should not happen
        LOG(FATAL, "mismatched exponents; this is most peculiar");
        exit(EXIT_FAILURE);
    }
    if (mpz_cmp(redo->w, session->w) != 0) {
        LOG(ERR, "INVALID %s -> %s", previous->metadata, session->metadata);
    }

    // replace validated session file with updated n_validations
    redo->n_validations += 1;
    session_save(redo, session->metadata);

    // clean up
    session_delete(redo);
}

struct sessions_queue {
    struct session** sessions;
    size_t n_sessions;
    size_t last;
    int min_validations;
    pthread_mutex_t lock;
};

static void* worker(void* argument) {
    struct sessions_queue* queue = argument;

    while (1) {
        // obtain task (could be made lockless using C11 atomics)
        pthread_mutex_lock(&queue->lock);
        queue->last += 1;
        size_t task = queue->last;
        pthread_mutex_unlock(&queue->lock);

        if (task >= queue->n_sessions) {
            // no more work
            break;
        }

        if (queue->sessions[task]->n_validations > queue->min_validations) {
            // priorize validating least-validated sessions
            continue;
        }

        // complete the task
        validate_session(queue->sessions[task], queue->sessions[task-1]);
    }

    return NULL;
}

int main(int argc, char** argv) {
    // allocate array of session pointers
    size_t n_sessions = (size_t) argc;  // number of arguments + 1 from scratch
    struct session** sessions = malloc(sizeof(struct session*) * n_sessions);
    if (sessions == NULL) {
        LOG(FATAL, "failed to allocate memory for sessions");
        exit(EXIT_FAILURE);
    }

    // load sessions from files given in program arguments
    sessions[0] = session_new();
    for (int i = 1; i < argc; i += 1) {
        struct session* session = session_new();
        const char* filename = argv[i];
        if (session_load(session, filename) < 0) {
            LOG(FATAL, "failed to load session file '%s'", filename);
            exit(EXIT_FAILURE);
        }
        session->metadata = filename;
        sessions[i] = session;
    }

    // check compatibility of sessions
    for (size_t i = 1; i < n_sessions; i += 1) {
        if (!session_iscompat(sessions[i-1], sessions[i])) {
            LOG(FATAL, "session files '%s' and '%s' are not compatible",
                sessions[i-1]->metadata, sessions[i]->metadata);
            exit(EXIT_FAILURE);
        }
    }

    // sort sessions by progress
    qsort(sessions, n_sessions, sizeof(struct session*), session_cmp);

    // find the lowest number of validations made on any session
    // the first session (started from scratch) does not need to be validated
    int min_validations = sessions[1]->n_validations;
    for (size_t i = 2; i < n_sessions; i += 1) {
        if (sessions[i]->n_validations < min_validations) {
            min_validations = sessions[i]->n_validations;
        }
    }

    struct sessions_queue queue = {
        .sessions = sessions,
        .n_sessions = n_sessions,
        .last = 0,
        .min_validations = min_validations,
    };

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
    for (size_t i = 0; i < n_sessions; i += 1) {
        session_delete(sessions[i]);
    }
}
