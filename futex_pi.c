#define _GNU_SOURCE
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <linux/futex.h>

#define errExit(msg)        \
    {                       \
        perror(msg);        \
        exit(EXIT_FAILURE); \
    }

int futex(int *uaddr, int futex_op, int val, uint32_t val2, int *uaddr2, int val3)
{
    return syscall(SYS_futex, uaddr, futex_op, val, val2, uaddr2, val3);
}

void flock(int *uaddr)
{
    printf("[%u] Locking futex (%p = %u)\n", gettid(), uaddr, *uaddr);
    const int zero = 0;
    if (atomic_compare_exchange_strong(uaddr, &zero, gettid()))
    {
        printf("[%u] Locked from userspace.\n", gettid());
    }
    else
    {
        printf("[%u] Locking from kernelspace.\n", gettid());
        if (futex(uaddr, FUTEX_LOCK_PI, 0, 0, NULL, 0) == -1)
            errExit("futex-FUTEX_LOCK_PI");
    }
    printf("[%u] Locked futex (%p = %u)\n", gettid(), uaddr, *uaddr);
}

void funlock(int *uaddr)
{
    printf("[%u] Unlocking futex (%p = %u)\n", gettid(), uaddr, *uaddr);
    const int tid = gettid();
    if (atomic_compare_exchange_strong(uaddr, &tid, 0))
    {
        printf("[%u] Unlocked from userspace.\n", gettid());
    }
    else
    {
        printf("[%u] Unlocking from kernelspace.\n", gettid());
        if (futex(uaddr, FUTEX_UNLOCK_PI, 0, 0, NULL, 0) == -1)
            errExit("futex-FUTEX_UNLOCK_PI");
    }
    printf("[%u] Unlocked futex (%p = %u)\n", gettid(), uaddr, *uaddr);
}

int main()
{
    int *futex1, *futex2, *iaddr, child_pid;
    iaddr = mmap(NULL, sizeof(int) * 2, PROT_READ | PROT_WRITE,
                 MAP_ANONYMOUS | MAP_SHARED, -1, 0);
    if (iaddr == MAP_FAILED)
        errExit("mmap");

    futex1 = &iaddr[0];
    futex2 = &iaddr[1];

    *futex1 = 0;
    *futex2 = 0;

    // Child 1
    child_pid = fork();
    if (child_pid == -1)
        errExit("fork");
    if (child_pid == 0)
    {
        flock(futex1);
        sleep(5);
        funlock(futex1);
        puts("Child 1 exits.");
        exit(EXIT_SUCCESS);
    }

    // Child 2
    child_pid = fork();
    if (child_pid == -1)
        errExit("fork");
    if (child_pid == 0)
    {
        sleep(1);
        flock(futex1);
        puts("Child 2 exits.");
        sleep(1);
        exit(EXIT_SUCCESS);
    }

    // Child 3
    child_pid = fork();
    if (child_pid == -1)
        errExit("fork");
    if (child_pid == 0)
    {
        sleep(1);
        flock(futex1);
        puts("Child 3 exits.");
        sleep(1);
        exit(EXIT_SUCCESS);
    }

    wait(NULL);
    wait(NULL);
    wait(NULL);
}