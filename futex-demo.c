#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/futex.h>
// #include "futex.h"

#define errExit(msg)        \
    {                       \
        perror(msg);        \
        exit(EXIT_FAILURE); \
    }

int *futex1, *futex2, *iaddr;

int futex(int *uaddr, int futex_op, int val, uint32_t val2, int *uaddr2, int val3)
{
    return syscall(SYS_futex, uaddr, futex_op, val, val2, uaddr2, val3);
}

void fwait(int *futexp)
{
    int s;
    printf("[%d] Waiting (%p)\n", getpid(), futexp);
    s = futex(futexp, FUTEX_WAIT, 0, 0, NULL, 0);
    printf("[%d] Woken\n", getpid());
    if (s == -1)
        errExit("futex-FUTEX_WAIT");
}

void fwake(int *futexp, int nwake)
{
    int s;

    printf("[%d] Waking (%p)\n", getpid(), futexp);
    s = futex(futexp, FUTEX_WAKE, nwake, 0, NULL, 0);
    if (s == -1)
        errExit("futex-FUTEX_WAKE");
}

void frequeue(int *futexsrc, int *futexdst, int nwake, int waiter_limit)
{
    int s;
    printf("[%d] Requeueing (%p --> %p)\n", getpid(), futexsrc, futexdst);
    s = futex(futexsrc, FUTEX_REQUEUE, nwake, waiter_limit, futexdst, 0);
    if (s == -1)
        errExit("futex-FUTEX_REQUEUE");
}

int main(int argc, char *argv[])
{
    pid_t childPid;

    setbuf(stdout, NULL);

    /* Create a shared anonymous mapping that will hold the futexes.
      Since the futexes are being shared between processes, we
      subsequently use the "shared" futex operations (i.e., not the
      ones suffixed "_PRIVATE") */

    iaddr = mmap(NULL, sizeof(int) * 2, PROT_READ | PROT_WRITE,
                 MAP_ANONYMOUS | MAP_SHARED, -1, 0);
    if (iaddr == MAP_FAILED)
        errExit("mmap");

    futex1 = &iaddr[0];
    futex2 = &iaddr[1];

    *futex1 = 0;
    *futex2 = 0;

    /* Create a child process that inherits the shared anonymous
      mapping */

    childPid = fork();
    if (childPid == -1)
        errExit("fork");

    if (childPid == 0)
    { /* Child */
        printf("[%d] Child1\n", getpid());
        fwait(futex1);

        printf("Child1 exit\n");
        exit(EXIT_SUCCESS);
    }

    /* Parent falls through to here */
    childPid = fork();
    if (childPid == -1)
        errExit("fork");

    if (childPid == 0)
    { /* Child */
        printf("[%d] Child2\n", getpid());
        fwait(futex1);

        printf("Child2 exit\n");
        exit(EXIT_SUCCESS);
    }

    printf("[%d] Parent\n", getpid());
    sleep(1);
    frequeue(futex1, futex2, 1, 1);
    sleep(1);
    fwake(futex2, 1);

    sleep(1);
    wait(NULL);
    exit(EXIT_SUCCESS);
}
