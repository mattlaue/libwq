#ifndef __WQ_H__
#define __WQ_H__

#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WORKQUEUE_DEFAULT_MAX_WORKERS 32
#define WORKQUEUE_DEFAULT_TIMEOUT 10

#define WORKQUEUE_READ_PIPE 0
#define WORKQUEUE_WRITE_PIPE 1

struct workqueue;

typedef void (* workqueue_trace_func_t)(void *, const char *, ...);

typedef struct workqueue_stat {
    unsigned int available;
    unsigned int current;
    bool shutdown;
} workqueue_stat_t;

typedef struct workqueue_backend {
    const char *name;
    int (*init)(struct workqueue *);
    void (*shutdown)(struct workqueue *);
    void (*destroy)(struct workqueue *);
    void (*lock)(struct workqueue *);
    void (*unlock)(struct workqueue *);
    bool (*locked)(struct workqueue *);
    int (*wait)(struct workqueue *, unsigned int);
    void (*submit)(struct workqueue *);
    int (*stat)(struct workqueue *, struct workqueue_stat *);
    int (*worker_create)(struct workqueue *, void *(*func)(void *));
    void (*worker_start)(struct workqueue *);
    int (*worker_wait)(struct workqueue *);
    void (*worker_finish)(struct workqueue *);
    void (*worker_idle)(struct workqueue *);
    void (*worker_busy)(struct workqueue *);
    void (*worker_complete)(struct workqueue *);
    int (*self)(struct workqueue *);
} workqueue_backend_t;

#ifdef __WIN32
#include <windows.h>
#define PIPE HANDLE
#else
#define PIPE int
#endif

/* This struct should be considered read-only by the backend. */
typedef struct workqueue {
    PIPE pipefds[2];
    unsigned int max_workers;
    unsigned int timeout;
    workqueue_backend_t *backend;
    void *private;
} workqueue_t;

typedef struct work_item {
    void (*func)(int, void *);
    void *arg;
} work_item_t;

int workqueue_init(workqueue_t *wq, const char *name);
void workqueue_destroy(workqueue_t *wq);
int workqueue_submit(workqueue_t *wq, void (* func)(int, void *), void *arg);

/* can be called without the lock held, but doesn't have much meaning. */
bool workqueue_idle(workqueue_t *wq);
/* must be called with wq locked or returns EPERM */
int workqueue_wait(workqueue_t *wq, unsigned int timeout);

void workqueue_trace(workqueue_trace_func_t func, void *data);
void workqueue_fprintf(void *, const char *fmt, ...);

void workqueue_lock(workqueue_t *wq);
void workqueue_unlock(workqueue_t *wq);
bool workqueue_locked(workqueue_t *wq);

static inline int
workqueue_self(workqueue_t *wq)
{
    assert(wq->backend->self != NULL);
    return wq->backend->self(wq);
}

#ifdef __cplusplus
}
#endif

#endif /* __WQ_H__ */
