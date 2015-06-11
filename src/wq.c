/* Copyright (C) 2012 Akiri Solutions, Inc.
   http://www.akirisolutions.com

   wq - A general purpose work-queue library for C/C++.

   The logr package is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The logr package is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the logr source code; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>

#ifdef __WIN32
#include <windows.h>
#endif

#include "wq.h"
#include "pipe.h"

#ifndef ETIMEDOUT
#define ETIMEDOUT 145
#endif

static workqueue_backend_t *workqueue_backends[];

static workqueue_trace_func_t workqueue_trace_func;
static void *workqueue_trace_data;

#define TRACE(fmt, args ...) do {                                       \
        if (workqueue_trace_func != NULL) {                             \
            workqueue_trace_func(workqueue_trace_data,                  \
                                 "[%s] "fmt,                            \
                                 __FUNCTION__,                          \
                                 ## args);                              \
        }                                                               \
    } while(0)

#define WERROR(fmt, args ...) do {                                       \
        if (workqueue_trace_func != NULL) {                             \
            workqueue_trace_func(workqueue_trace_data,                  \
                                 "[%s] *** ERROR *** "fmt,              \
                                 __FUNCTION__,                          \
                                 ## args);                              \
        }                                                               \
    } while(0)

#define WTRACE(wq, fmt, args ...) do {                                  \
        if (workqueue_trace_func != NULL) {                             \
            workqueue_trace_func(workqueue_trace_data,                  \
                                 "%04d[%s] "fmt,                        \
                                 workqueue_self(wq),                    \
                                 __FUNCTION__,                          \
                                 ## args);                              \
        }                                                               \
    } while(0)

void
workqueue_lock(workqueue_t *wq)
{
    assert(wq->backend->lock != NULL);
    wq->backend->lock(wq);
    WTRACE(wq, "\n");
}

void
workqueue_unlock(workqueue_t *wq)
{
    WTRACE(wq, "\n");
    assert(wq->backend->unlock != NULL);
    wq->backend->unlock(wq);
}

bool
workqueue_locked(workqueue_t *wq)
{
    assert(wq->backend->locked != NULL);
    return wq->backend->locked(wq);
}

static inline void
workqueue_backend_shutdown(workqueue_t *wq)
{
    if (wq->backend->shutdown) {
        wq->backend->shutdown(wq);
    }
}

static inline void
workqueue_backend_destroy(workqueue_t *wq)
{
    if (wq->backend->destroy) {
        wq->backend->destroy(wq);
    }
}

static inline int
workqueue_backend_worker_wait(workqueue_t *wq)
{
    if (wq->backend->worker_wait) {
        return wq->backend->worker_wait(wq);
    }
    return EINVAL;
}

static inline int
workqueue_backend_worker_create(workqueue_t *wq, void *(*func)(void *))
{
    if (wq->backend->worker_create) {
        return wq->backend->worker_create(wq, func);
    }
    return EINVAL;
}

static inline void
workqueue_backend_worker_start(workqueue_t *wq)
{
    if (wq->backend->worker_start) {
        wq->backend->worker_start(wq);
    }
}

static inline void
workqueue_backend_worker_busy(workqueue_t *wq)
{
    if (wq->backend->worker_busy) {
        wq->backend->worker_busy(wq);
    }
}

static inline void
workqueue_backend_worker_complete(workqueue_t *wq)
{
    if (wq->backend->worker_complete) {
        wq->backend->worker_complete(wq);
    }
}

static inline void
workqueue_backend_worker_idle(workqueue_t *wq)
{
    if (wq->backend->worker_idle) {
        wq->backend->worker_idle(wq);
    }
}

static inline void
workqueue_backend_worker_finish(workqueue_t *wq)
{
    if (wq->backend->worker_finish) {
        wq->backend->worker_finish(wq);
    }
}

static inline void
workqueue_backend_submit(workqueue_t *wq)
{
    assert (wq->backend->submit != NULL);
    wq->backend->submit(wq);
}

static inline int
workqueue_backend_stat(workqueue_t *wq, workqueue_stat_t *st)
{
    assert(wq->backend->stat != NULL);
    return wq->backend->stat(wq, st);
}

bool
workqueue_idle(workqueue_t *wq)
{
    workqueue_stat_t st;
    workqueue_backend_stat(wq, &st);
    return (st.available == st.current);
}

int
workqueue_wait(workqueue_t *wq, unsigned int timeout)
{
    int rc;
    if (!workqueue_locked(wq)) {
        WTRACE(wq, "workqueue not locked: %s\n", strerror(EPERM));
        return EPERM;
    }
    assert(wq->backend->wait);
    rc = wq->backend->wait(wq, timeout);
    if (rc == 0) {
        TRACE("\n");
    } else if (rc == ETIMEDOUT) {
        TRACE("timeout.\n");
    } else {
        WERROR("%s\n", strerror(errno));
    }
    return rc;
}

static inline workqueue_backend_t *
workqueue_find_backend(const char *name)
{
    int i;

    for (i = 0; (workqueue_backends[i] != NULL); i++) {
        workqueue_backend_t *p = workqueue_backends[i];
        if (strcmp(p->name, name) == 0) {
            return p;
        }
    }
    return NULL;
}

int
workqueue_init(workqueue_t *wq, const char *name)
{
    int rc;
    workqueue_backend_t *backend = NULL;

    TRACE("%s\n", name);

    memset(wq, 0, sizeof(workqueue_t));

    if (name != NULL) {
        backend = workqueue_find_backend(name);
    } else {
        backend = workqueue_backends[0];
    }

    if (backend == NULL) {
        errno = EINVAL;
        return -1;
    }
    wq->backend = backend;

    rc = pipe(wq->pipefds);
    if (rc < 0) {
        WERROR("pipe() failed: %s\n", strerror(errno));
        return rc;
    }

    rc = pipe_set_nonblocking(wq->pipefds[WORKQUEUE_READ_PIPE]);
    if (rc < 0) {
        WERROR("pipe_set_nonblocking() failed.\n");
        goto error;
    }

    wq->max_workers = WORKQUEUE_DEFAULT_MAX_WORKERS;
    wq->timeout = WORKQUEUE_DEFAULT_TIMEOUT;

    if (wq->backend->init) {
        rc = wq->backend->init(wq);
        if (rc < 0) {
            goto error;
        }
    }
    return 0;

error:
    rc = errno;
    close_pipe(wq->pipefds[WORKQUEUE_READ_PIPE]);
    close_pipe(wq->pipefds[WORKQUEUE_WRITE_PIPE]);
    errno = rc;
    return -1;
}

void
workqueue_destroy(workqueue_t *wq)
{
    TRACE("\n");
    workqueue_lock(wq);
    workqueue_backend_shutdown(wq);
    close_pipe(wq->pipefds[WORKQUEUE_WRITE_PIPE]);
    workqueue_backend_destroy(wq);
    TRACE("done\n");
    //workqueue_unlock(wq);
}

static int
workqueue_getitem(workqueue_t *wq, work_item_t *item)
{
    int rc;
    workqueue_stat_t st;

    while (1) {
        workqueue_backend_stat(wq, &st);
        if (st.shutdown) {
            return -1;
        }

        /* This read is atomic as long as sizeof(work_item_t) <= PIPE_BUF */
        rc = read_pipe(wq->pipefds[WORKQUEUE_READ_PIPE],
                       item, sizeof(work_item_t));
        if (rc == 0) {
            WTRACE(wq, "exiting.\n");
            return -1;
        }
        if (rc < 0 && errno != EWOULDBLOCK) {
            WERROR("read_pipe() failed: %s (%d)\n", strerror(errno), errno);
            return errno;
        }
        if (rc == sizeof(work_item_t)) {
            break;
        }

        rc = workqueue_backend_worker_wait(wq);
        if (rc == ETIMEDOUT) {
            WTRACE(wq, "timeout.\n");
            return rc;
        } else if (rc != 0) {
            WERROR("workqueue_backend_wait() failed: %s\n", strerror(rc));
            return rc;
        }
    }
    WTRACE(wq, "\n");
    return 0;
}

static void *
workqueue_worker(void *arg)
{
    workqueue_t *wq = (workqueue_t *)arg;
    workqueue_stat_t st;

    workqueue_lock(wq);
    workqueue_backend_worker_start(wq);
    workqueue_unlock(wq);

    /* self() is guaranteed to work after worker_start... */
    WTRACE(wq, "start\n");

    while (1) {
        int rc = 0;
        work_item_t item;

        workqueue_lock(wq);

        rc = workqueue_getitem(wq, &item);
        if (rc != 0)
            break;

        workqueue_backend_worker_busy(wq);
        workqueue_unlock(wq);

        WTRACE(wq, "func()\n");
        item.func(workqueue_self(wq), item.arg);

        workqueue_lock(wq);
        workqueue_backend_worker_complete(wq);
        workqueue_backend_worker_idle(wq);
        workqueue_unlock(wq);
    }

    workqueue_backend_worker_finish(wq);
    workqueue_backend_stat(wq, &st);
    WTRACE(wq, "worker exiting: current=%d\n", st.current);

    workqueue_unlock(wq);

    return NULL;
}

int
workqueue_submit(workqueue_t *wq, void (* func)(int, void *), void *arg)
{
    int rc;
    work_item_t item;
    workqueue_stat_t st;

    if (wq == NULL || func == NULL) {
        errno = EINVAL;
        return -1;
    }

    item.func = func;
    item.arg = arg;

    WTRACE(wq, "func=%p arg=%p\n", func, arg);

    workqueue_lock(wq);
    workqueue_backend_stat(wq, &st);
    if (st.available == 0 && st.current < wq->max_workers) {
        rc = workqueue_backend_worker_create(wq, workqueue_worker);
        if (rc == 0) {
            /* This is a slight fib since the worker is counted before
               it actually starts. */
            WTRACE(wq, "worker created: current=%d\n", st.current+1);
        } else {
            WERROR("worker creation failed: %s\n", strerror(rc));
        }
    }
    workqueue_unlock(wq);

    /* This write is guaranteed to be atomic. */
    rc = write_pipe(wq->pipefds[WORKQUEUE_WRITE_PIPE], &item, sizeof(item));
    if (rc < 0) return rc;

    workqueue_backend_submit(wq);
    return 0;
}

void
workqueue_fprintf(void *arg, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf((FILE *)arg, fmt, ap);
    va_end(ap);
}

void
workqueue_trace(workqueue_trace_func_t func, void *data)
{
    workqueue_trace_func = func;
    workqueue_trace_data = data;
}

extern workqueue_backend_t workqueue_thread_backend;
#ifndef __WIN32
extern workqueue_backend_t workqueue_process_backend;
#endif

static workqueue_backend_t *workqueue_backends[] = {
    &workqueue_thread_backend,
#ifndef __WIN32
    &workqueue_process_backend,
#endif
    NULL,
};

