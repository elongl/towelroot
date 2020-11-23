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

int *non_pi_futex, *pi_futex;

int futex(int *uaddr, int futex_op, int val, uint32_t val2, int *uaddr2, int val3)
{
    return syscall(SYS_futex, uaddr, futex_op, val, val2, uaddr2, val3);
}

void flock(int *uaddr)
{
    const int tid = gettid();
    printf("[%u] Locking futex (%p = %u)\n", tid, uaddr, *uaddr);
    const int zero = 0;
    if (atomic_compare_exchange_strong(uaddr, &zero, tid))
    {
        printf("[%u] Locked from userspace.\n", tid);
    }
    else
    {
        printf("[%u] Locking from kernelspace.\n", tid);
        if (futex(uaddr, FUTEX_LOCK_PI, 0, 0, NULL, 0) == -1)
            errExit("futex-FUTEX_LOCK_PI");
    }
    printf("[%u] Locked futex.\n", tid, uaddr, *uaddr);
}

void funlock(int *uaddr)
{
    const int tid = gettid();
    printf("[%u] Unlocking futex (%p = %u)\n", tid, uaddr, *uaddr);
    if (atomic_compare_exchange_strong(uaddr, &tid, 0))
    {
        printf("[%u] Unlocked from userspace.\n", tid);
    }
    else
    {
        printf("[%u] Unlocking from kernelspace.\n", tid);
        if (futex(uaddr, FUTEX_UNLOCK_PI, 0, 0, NULL, 0) == -1)
            errExit("futex-FUTEX_UNLOCK_PI");
    }
    printf("[%u] Unlocked futex.\n", tid, uaddr, *uaddr);
}

void fwait_requeue(int *src, int *dst, int expected_val)
{
    const int tid = gettid();
    printf("[%u] Waiting on futex %p, potentially requeued to %p\n", tid, src, dst);
    if (futex(src, FUTEX_WAIT_REQUEUE_PI, expected_val, 0, dst, 0) == -1)
        errExit("futex-FUTEX_WAIT_REQUEUE_PI");
    printf("[%u] Woke up.\n", tid, src);
}

void frequeue(int *src, int *dst, int max_requeued_waiters, int expected_val)
{
    const int tid = gettid();
    printf("[%u] Requeuing %p --> %p\n", tid, src, dst);
    if (futex(src, FUTEX_CMP_REQUEUE_PI, 1, max_requeued_waiters, dst, expected_val) == -1)
        errExit("futex-FUTEX_CMP_REQUEUE_PI");
    printf("[%u] Done requeueing.", tid);
}

int main()
{
}