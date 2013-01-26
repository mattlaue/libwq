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
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>

#include <wq.h>

typedef struct workqueue_process_private {
    pthread_mutex_t mutex;
    pthread_mutexattr_t mutexattr;
    pthread_cond_t work_cond;
    pthread_cond_t completion_cond;
    pthread_cond_t shutdown_cond;
    pthread_condattr_t condattr;
    workqueue_stat_t st;
} workqueue_process_private_t;

extern int wq_gettime(struct timespec *tp);

static void
_workqueue_process_sigchild(int sig, siginfo_t *si, void *unused)
{
    pid_t pid;

    do {
        pid = waitpid(-1, NULL, WNOHANG);
    } while (pid > 0);
}

static int
workqueue_process_init(workqueue_t *wq)
{
    int shmid, rc;
    workqueue_process_private_t *private;
    struct sigaction sa;

    shmid = shmget(IPC_PRIVATE,
                   sizeof(workqueue_process_private_t),
                   (0640 | IPC_CREAT));
    if (shmid < 0) {
        return -1;
    }

    private = shmat(shmid, NULL, 0);
    if (private == ((void *)-1)) {
        return -1;
    }

    memset(private, 0, sizeof(workqueue_process_private_t));
    private->st.shutdown = false;

    rc = pthread_mutexattr_init(&private->mutexattr);
    if (rc < 0) {
        shmdt(private);
        return -1;
    }
    pthread_mutexattr_setpshared(&private->mutexattr, PTHREAD_PROCESS_SHARED);

    rc = pthread_condattr_init(&private->condattr);
    if (rc < 0) {
        pthread_mutexattr_destroy(&private->mutexattr);
        shmdt(private);
        return -1;
    }
    pthread_condattr_setpshared(&private->condattr, PTHREAD_PROCESS_SHARED);

    pthread_mutex_init(&private->mutex, &private->mutexattr);
    pthread_cond_init(&private->work_cond, &private->condattr);
    pthread_cond_init(&private->completion_cond, &private->condattr);
    pthread_cond_init(&private->shutdown_cond, &private->condattr);


    sigaction(SIGCHLD, NULL, &sa);
    if (sa.sa_handler == NULL && sa.sa_sigaction == NULL) {
        sa.sa_flags = SA_SIGINFO | SA_NOCLDSTOP;
        sigemptyset(&sa.sa_mask);
        sa.sa_sigaction = _workqueue_process_sigchild;
        sigaction(SIGCHLD, &sa, NULL);
    }

    wq->private = private;
    return 0;
}

static void
workqueue_process_destroy(workqueue_t *wq)
{
    workqueue_process_private_t *private = wq->private;
    assert(private != NULL);
    pthread_mutexattr_destroy(&private->mutexattr);
    pthread_condattr_destroy(&private->condattr);
    shmdt(private);
}

static void
workqueue_process_shutdown(workqueue_t *wq)
{
    int rc = 0;
    workqueue_process_private_t *private = wq->private;

    private->st.shutdown = true;
    pthread_cond_broadcast(&private->work_cond);
    close(wq->pipefds[WORKQUEUE_READ_PIPE]);

    while (private->st.current > 0 && rc == 0) {
        rc = pthread_cond_wait(&private->shutdown_cond, &private->mutex);
    }
}

static bool
_workqueue_process_locked(workqueue_process_private_t *private)
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

static bool
workqueue_process_locked(workqueue_t *wq)
{
    workqueue_process_private_t *private = wq->private;
    return _workqueue_process_locked(private);
}

static void
workqueue_process_lock(workqueue_t *wq)
{
    workqueue_process_private_t *private = wq->private;
    pthread_mutex_lock(&private->mutex);
}

static void
workqueue_process_unlock(workqueue_t *wq)
{
    workqueue_process_private_t *private = wq->private;
    assert(_workqueue_process_locked(private));
    pthread_mutex_unlock(&private->mutex);
}

static int
_workqueue_process_cond_wait(pthread_cond_t *cond,
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
workqueue_process_submit(struct workqueue *wq)
{
    workqueue_process_private_t *private = wq->private;
    pthread_cond_signal(&private->work_cond);
}

static int
workqueue_process_wait(struct workqueue *wq, unsigned int timeout)
{
    workqueue_process_private_t *private = wq->private;
    return _workqueue_process_cond_wait(&private->completion_cond,
                                       &private->mutex,
                                       timeout);
}

static int
workqueue_process_stat(workqueue_t *wq, workqueue_stat_t *st)
{
    workqueue_process_private_t *private = wq->private;
    assert(_workqueue_process_locked(private));
    *st = private->st;
    return 0;
}

static int
workqueue_process_worker_create(struct workqueue *wq, void *(*func)(void *))
{
    sigset_t set, oldset;
    pid_t pid;

    workqueue_process_private_t *private = wq->private;
    assert(_workqueue_process_locked(private));

    sigfillset(&set);
    sigprocmask(SIG_BLOCK, &set, &oldset);

    pid = fork();
    if (pid == 0) {
        // child
        func(wq);
        exit(0);
    } else if (pid < 0) {
        // failed
        return -1;
    } else {
        // parent
        private->st.current++;
    }

    sigprocmask(SIG_SETMASK, &oldset, NULL);
    return 0;
}

static void
workqueue_process_worker_start(struct workqueue *wq)
{
    workqueue_process_private_t *private = wq->private;
    assert(_workqueue_process_locked(private));
    private->st.available++;
}


static int
workqueue_process_worker_wait(workqueue_t *wq)
{
    workqueue_process_private_t *private = wq->private;
    return _workqueue_process_cond_wait(&private->work_cond,
                                       &private->mutex,
                                       wq->timeout);
}

static void
workqueue_process_worker_finish(struct workqueue *wq)
{
    workqueue_process_private_t *private = wq->private;
    assert(_workqueue_process_locked(private));
    private->st.available--;
    private->st.current--;
    pthread_cond_signal(&private->shutdown_cond);
}

static void
workqueue_process_worker_idle(struct workqueue *wq)
{
    workqueue_process_private_t *private = wq->private;
    assert(_workqueue_process_locked(private));
    private->st.available++;
}

static void
workqueue_process_worker_busy(struct workqueue *wq)
{
    workqueue_process_private_t *private = wq->private;
    assert(_workqueue_process_locked(private));
    private->st.available--;
}

static void
workqueue_process_worker_complete(struct workqueue *wq)
{
    workqueue_process_private_t *private = wq->private;
    pthread_cond_broadcast(&private->completion_cond);
}

static int
workqueue_process_self(workqueue_t *wq)
{
    return getpid();
}

workqueue_backend_t
workqueue_process_backend = {
    .name = "process",
    .init = workqueue_process_init,
    .shutdown = workqueue_process_shutdown,
    .destroy = workqueue_process_destroy,
    .lock = workqueue_process_lock,
    .unlock = workqueue_process_unlock,
    .locked = workqueue_process_locked,
    .submit = workqueue_process_submit,
    .wait = workqueue_process_wait,
    .stat = workqueue_process_stat,

    .worker_create = workqueue_process_worker_create,
    .worker_start = workqueue_process_worker_start,
    .worker_wait = workqueue_process_worker_wait,
    .worker_idle = workqueue_process_worker_idle,
    .worker_busy = workqueue_process_worker_busy,
    .worker_complete = workqueue_process_worker_complete,
    .worker_finish = workqueue_process_worker_finish,

    .self = workqueue_process_self,
};
