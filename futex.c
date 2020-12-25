#define _GNU_SOURCE
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include "errexit.h"

int futex(int *uaddr, int futex_op, int val, uint32_t val2, int *uaddr2, int val3)
{
    return syscall(SYS_futex, uaddr, futex_op, val, val2, uaddr2, val3);
}

void flock(int *uaddr)
{
    const int pid = getpid();
    printf("[%u] Locking futex (%p = %u)\n", pid, uaddr, *uaddr);
    const int zero = 0;
    if (atomic_compare_exchange_strong(uaddr, &zero, pid))
        printf("[%u] Locked from userspace.\n", pid);
    else
    {
        printf("[%u] Locking from kernelspace.\n", pid);
        if (futex(uaddr, FUTEX_LOCK_PI, 0, 0, NULL, 0) == -1)
            ERR_EXIT("futex-FUTEX_LOCK_PI");
        printf("[%u] Locked futex.\n", pid);
    }
}

void funlock(int *uaddr)
{
    int pid = getpid();
    printf("[%u] Unlocking futex (%p = %u)\n", pid, uaddr, *uaddr);
    if (atomic_compare_exchange_strong(uaddr, &pid, 0))
        printf("[%u] Unlocked from userspace.\n", pid);
    else
    {
        pid = getpid();
        printf("[%u] Unlocking from kernelspace.\n", pid);
        if (futex(uaddr, FUTEX_UNLOCK_PI, 0, 0, NULL, 0) == -1)
            ERR_EXIT("futex-FUTEX_UNLOCK_PI");
        printf("[%u] Unlocked futex.\n", pid);
    }
}

void fwait_requeue(int *src, int *dst, int expected_val)
{
    const int pid = getpid();
    printf("[%u] Waiting on futex %p, potentially requeued to %p\n", pid, src, dst);
    if (futex(src, FUTEX_WAIT_REQUEUE_PI, expected_val, 0, dst, 0) == -1)
        ERR_EXIT("futex-FUTEX_WAIT_REQUEUE_PI");
    printf("[%u] Woke up.\n", pid);
}

void frequeue(int *src, int *dst, int max_requeued_waiters, int expected_val)
{
    const int pid = getpid();
    printf("[%u] Requeuing %p --> %p\n", pid, src, dst);
    if (futex(src, FUTEX_CMP_REQUEUE_PI, 1, max_requeued_waiters, dst, expected_val) == -1)
        ERR_EXIT("futex-FUTEX_CMP_REQUEUE_PI");
    printf("[%u] Done requeueing.\n", pid);
}
