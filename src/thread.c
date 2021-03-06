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
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <wq.h>

typedef struct workqueue_thread_private {
    pthread_mutex_t mutex;
    pthread_cond_t work_cond;
    pthread_cond_t completion_cond;
    pthread_cond_t shutdown_cond;
    pthread_key_t key;
    workqueue_stat_t st;
    int n;
} workqueue_thread_private_t;

extern int wq_gettime(struct timespec *tp);

static bool
_workqueue_thread_locked(workqueue_thread_private_t *private)
{
    int rc;

    rc = pthread_mutex_trylock(&private->mutex);
    if (rc == EBUSY) {
        return true;
    } else if (rc != 0) {
        return false;
    }
    pthread_mutex_unlock(&private->mutex);
    return false;
}

static int
workqueue_thread_init(workqueue_t *wq)
{
    workqueue_thread_private_t *private;
    int rc;

    private = malloc(sizeof(workqueue_thread_private_t));
    if (private == NULL) {
        return -1;
    }
    memset(private, 0, sizeof(workqueue_thread_private_t));
    private->st.shutdown = false;

    pthread_mutex_init(&private->mutex, NULL);
    pthread_cond_init(&private->work_cond, NULL);
    pthread_cond_init(&private->completion_cond, NULL);
    pthread_cond_init(&private->shutdown_cond, NULL);

    rc = pthread_key_create(&private->key, NULL);
    if (rc < 0) {
        free(private);
        return -1;
    }

    wq->private = private;
    return 0;
}

static void
workqueue_thread_destroy(workqueue_t *wq)
{
    if (wq->private) {
        workqueue_thread_private_t *private = wq->private;
        pthread_key_delete(private->key);
        free(wq->private);
    }
}

static void
workqueue_thread_shutdown(workqueue_t *wq)
{
    int rc = 0;
    workqueue_thread_private_t *private = wq->private;
    assert(_workqueue_thread_locked(private));

    private->st.shutdown = true;
    pthread_cond_broadcast(&private->work_cond);
    close(wq->pipefds[WORKQUEUE_READ_PIPE]);

    while (private->st.current > 0 && rc == 0) {
        rc = pthread_cond_wait(&private->shutdown_cond, &private->mutex);
    }
}

static bool
workqueue_thread_locked(workqueue_t *wq)
{
    workqueue_thread_private_t *private = wq->private;
    return _workqueue_thread_locked(private);
}

static void
workqueue_thread_lock(workqueue_t *wq)
{
    workqueue_thread_private_t *private = wq->private;
    pthread_mutex_lock(&private->mutex);
}

static void
workqueue_thread_unlock(workqueue_t *wq)
{
    workqueue_thread_private_t *private = wq->private;
    assert(_workqueue_thread_locked(private));
    pthread_mutex_unlock(&private->mutex);
}

static int
_workqueue_thread_cond_wait(pthread_cond_t *cond,
                            pthread_mutex_t *mutex,
                            unsigned int timeout)
{
    int rc = 0;

    if (timeout) {
        struct timespec ts;
        wq_gettime(&ts);
        ts.tv_sec += timeout;
        rc = pthread_cond_timedwait(cond, mutex, &ts);
        if (rc == ETIMEDOUT) {
            return rc;
        }
    } else {
        rc = pthread_cond_wait(cond, mutex);
    }

    return rc;
}

static void
workqueue_thread_submit(struct workqueue *wq)
{
    workqueue_thread_private_t *private = wq->private;
    pthread_cond_signal(&private->work_cond);
}

static int
workqueue_thread_wait(struct workqueue *wq, unsigned int timeout)
{
    workqueue_thread_private_t *private = wq->private;
    return _workqueue_thread_cond_wait(&private->completion_cond,
                                       &private->mutex,
                                       timeout);
}

static int
workqueue_thread_stat(workqueue_t *wq, workqueue_stat_t *st)
{
    workqueue_thread_private_t *private = wq->private;
    assert(_workqueue_thread_locked(private));
    *st = private->st;
    return 0;
}

static int
workqueue_thread_worker_create(struct workqueue *wq, void *(*func)(void *))
{
    int rc;
    pthread_t t;
    sigset_t set, oldset;

    workqueue_thread_private_t *private = wq->private;
    assert(_workqueue_thread_locked(private));

    sigfillset(&set);
    sigprocmask(SIG_BLOCK, &set, &oldset);
    rc = pthread_create(&t, NULL, func, wq);
    sigprocmask(SIG_SETMASK, &oldset, NULL);

    if (rc == 0) {
        pthread_detach(t);
        private->st.current++;
    }
    return rc;
}

static void
workqueue_thread_worker_start(struct workqueue *wq)
{
    workqueue_thread_private_t *private = wq->private;
    unsigned long id;

    assert(_workqueue_thread_locked(private));
    private->st.available++;
    id = ++private->n;
    pthread_setspecific(private->key, (void *)id);
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
}


static int
workqueue_thread_worker_wait(workqueue_t *wq)
{
    workqueue_thread_private_t *private = wq->private;
    return _workqueue_thread_cond_wait(&private->work_cond,
                                       &private->mutex,
                                       wq->timeout);
}

static void
workqueue_thread_worker_finish(struct workqueue *wq)
{
    workqueue_thread_private_t *private = wq->private;
    assert(_workqueue_thread_locked(private));
    private->st.available--;
    private->st.current--;
    pthread_cond_signal(&private->shutdown_cond);
}

static void
workqueue_thread_worker_idle(struct workqueue *wq)
{
    workqueue_thread_private_t *private = wq->private;
    assert(_workqueue_thread_locked(private));
    private->st.available++;
}

static void
workqueue_thread_worker_busy(struct workqueue *wq)
{
    workqueue_thread_private_t *private = wq->private;
    assert(_workqueue_thread_locked(private));
    private->st.available--;
}

static void
workqueue_thread_worker_complete(struct workqueue *wq)
{
    workqueue_thread_private_t *private = wq->private;
    pthread_cond_broadcast(&private->completion_cond);
}

static int
workqueue_thread_self(workqueue_t *wq)
{
    workqueue_thread_private_t *private = wq->private;
    unsigned long id = (unsigned long)pthread_getspecific(private->key);
    return (int)id;
}

workqueue_backend_t
workqueue_thread_backend = {
    .name = "thread",
    .init = workqueue_thread_init,
    .shutdown = workqueue_thread_shutdown,
    .destroy = workqueue_thread_destroy,
    .lock = workqueue_thread_lock,
    .unlock = workqueue_thread_unlock,
    .locked = workqueue_thread_locked,
    .submit = workqueue_thread_submit,
    .wait = workqueue_thread_wait,
    .stat = workqueue_thread_stat,

    .worker_create = workqueue_thread_worker_create,
    .worker_start = workqueue_thread_worker_start,
    .worker_wait = workqueue_thread_worker_wait,
    .worker_idle = workqueue_thread_worker_idle,
    .worker_busy = workqueue_thread_worker_busy,
    .worker_complete = workqueue_thread_worker_complete,
    .worker_finish = workqueue_thread_worker_finish,

    .self = workqueue_thread_self,
};
