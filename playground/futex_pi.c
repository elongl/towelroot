#define _GNU_SOURCE
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/syscall.h>
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
    printf("Locking futex (%p = %u)\n", uaddr, *uaddr);
    const int zero = 0;
    if (atomic_compare_exchange_strong(uaddr, &zero, gettid()))
    {
        printf("Locked from userspace.\n");
    }
    else
    {
        printf("Locking from kernelspace.\n");
        if (futex(uaddr, FUTEX_LOCK_PI, 0, 0, NULL, 0) == -1)
            errExit("futex-FUTEX_LOCK_PI");
    }
    printf("Locked futex (%p = %u)\n", uaddr, *uaddr);
}

void funlock(int *uaddr)
{
    printf("Unlocking futex (%p = %u)\n", uaddr, *uaddr);
    const int tid = gettid();
    if (atomic_compare_exchange_strong(uaddr, &tid, 0))
    {
        printf("Unlocked from userspace.\n");
    }
    else
    {
        printf("Unlocking from kernelspace.\n");
        if (futex(uaddr, FUTEX_UNLOCK_PI, 0, 0, NULL, 0))
            errExit("futex-FUTEX_UNLOCK_PI");
    }
    printf("Unlocked futex (%p = %u)\n", uaddr, *uaddr);
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
        sleep(8);
        funlock(futex1);
        exit(EXIT_SUCCESS);
    }

    // Child 2
    child_pid = fork();
    if (child_pid == -1)
        errExit("fork");
    if (child_pid == 0)
    {
        sleep(5);
        flock(futex1);
        exit(EXIT_SUCCESS);
    }

    wait(NULL);
    wait(NULL);
}