#include <stdio.h>
#include <unistd.h>
#include <wq.h>

#ifdef __WIN32
#include <windows.h>
#define sleep(n) Sleep((n) * 1000)
#endif

static void
hello(int id, void *arg)
{
    long n = (long)(arg);

    /* sleep to ensure each item gets a new worker. */
    sleep(1);
    printf("%04d Hello World! (%ld)\n", id, n);
}

int
main(int argc, char **argv)
{
    int rc, opt;
    long i;
    workqueue_t wq;
    bool trace = false;

    while ((opt = getopt(argc, argv, "t")) != -1) {
	switch (opt) {
	case 't':
	    trace = true;
	    break;
	default:
	    fprintf(stderr, "Usage: %s [-t]\n", argv[0]);
	    exit(EXIT_FAILURE);
	}
    }

    if (trace) {
	workqueue_trace(workqueue_fprintf, stdout);
    }

    printf(" *** Using 'thread' backend. ***\n");
    rc = workqueue_init(&wq, "thread");
    if (rc < 0) {
	perror("workqueue_init(thread)");
	return -1;
    }

    for (i = 0; i < 10; i++) {
	rc = workqueue_submit(&wq, hello, (void *)(i+1));
	if (rc < 0) {
	    fprintf(stderr, "workqueue_submit(thread%ld)", i);
	    return -1;
	}
    }

    workqueue_lock(&wq);
    while (!workqueue_idle(&wq)) {
	workqueue_wait(&wq, 0);
    }
    workqueue_unlock(&wq);

    workqueue_destroy(&wq);

#ifndef __WIN32
    printf(" *** Using 'process' backend. ***\n");
    rc = workqueue_init(&wq, "process");
    if (rc < 0) {
	perror("workqueue_init(process)");
	return -1;
    }

    for (i = 0; i < 10; i++) {
	rc = workqueue_submit(&wq, hello, (void *)(i+1));
	if (rc < 0) {
	    fprintf(stderr, "workqueue_submit(thread%ld)", i);
	    return -1;
	}
    }

    workqueue_lock(&wq);
    while (!workqueue_idle(&wq)) {
	workqueue_wait(&wq, 0);
    }
    workqueue_unlock(&wq);

    workqueue_destroy(&wq);
#endif

    if (!trace) {
	printf(" *** Consider re-running this example with tracing enabled [-t]. ***\n");
    }

    return 0;
}
