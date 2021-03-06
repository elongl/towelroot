#include <stdio.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <assert.h>
#include "futex.h"

#define CRASH_SEC 3

int main()
{
    pid_t pid;
    uint32_t *futexes;
    uint32_t *non_pi_futex, *pi_futex;

    assert((futexes = mmap(NULL, sizeof(uint32_t) * 2, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0)) > 0);

    non_pi_futex = &futexes[0];
    pi_futex = &futexes[1];

    flock(pi_futex);

    assert((pid = fork()) != -1);
    if (!pid)
    {
        fwait_requeue(non_pi_futex, pi_futex, 0);
        puts("Child continues.");
        exit(EXIT_SUCCESS);
    }

    printf("Kernel will crash in %u seconds...\n", CRASH_SEC);
    sleep(CRASH_SEC);

    frequeue(non_pi_futex, pi_futex, 1, 0);
    *pi_futex = 0;
    frequeue(pi_futex, pi_futex, 1, 0);

    wait(NULL);
}
