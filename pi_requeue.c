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
    printf("[%u] Locked futex.\n", tid);
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
    printf("[%u] Unlocked futex.\n", tid);
}

void fwait_requeue(int *src, int *dst, int expected_val)
{
    const int tid = gettid();
    printf("[%u] Waiting on futex %p, potentially requeued to %p\n", tid, src, dst);
    if (futex(src, FUTEX_WAIT_REQUEUE_PI, expected_val, 0, dst, 0) == -1)
        errExit("futex-FUTEX_WAIT_REQUEUE_PI");
    printf("[%u] Woke up.\n", tid);
}

void frequeue(int *src, int *dst, int max_requeued_waiters, int expected_val)
{
    const int tid = gettid();
    printf("[%u] Requeuing %p --> %p\n", tid, src, dst);
    if (futex(src, FUTEX_CMP_REQUEUE_PI, 1, max_requeued_waiters, dst, expected_val) == -1)
        errExit("futex-FUTEX_CMP_REQUEUE_PI");
    printf("[%u] Done requeueing.\n", tid);
}

int main()
{
    int *futexes, *non_pi_futex, *pi_futex;

    futexes = mmap(NULL, sizeof(int) * 2, PROT_READ | PROT_WRITE,
                   MAP_ANONYMOUS | MAP_SHARED, -1, 0);
    if (futexes == MAP_FAILED)
        errExit("mmap");

    non_pi_futex = &futexes[0];
    pi_futex = &futexes[1];

    flock(pi_futex);

    int pid = fork();
    if (pid == -1)
        errExit("fork");
    if (pid == 0)
    {
        fwait_requeue(non_pi_futex, pi_futex, 0);
        sleep(3);
        puts("Child exiting.");
        exit(EXIT_SUCCESS);
    }

    sleep(3);
    frequeue(non_pi_futex, pi_futex, 1, 0);

    sleep(3);
    funlock(pi_futex);

    wait(NULL);
}