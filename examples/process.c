#include <stdio.h>
#include <assert.h>
#include <wq.h>

#ifdef __WIN32
#error "Process backend not yet implemented for WIN32."
#endif

static void
hello(int id, void *unused)
{
    printf("%04d Hello World!\n", id);
}

int
main(int argc, char **argv)
{
    int rc;
    workqueue_t wq;

    rc = workqueue_init(&wq, "process");
    assert(rc == 0);

    rc = workqueue_submit(&wq, hello, NULL);
    assert(rc == 0);

    workqueue_lock(&wq);
    while (!workqueue_idle(&wq)) {
        workqueue_wait(&wq, 0);
    }
    workqueue_unlock(&wq);

    workqueue_destroy(&wq);
    return 0;
}
