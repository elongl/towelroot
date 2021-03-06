#include <stdint.h>

int futex(int *uaddr, int futex_op, int val, uint32_t val2, int *uaddr2, int val3);
int flock(int *uaddr);
int funlock(int *uaddr);
int fwait_requeue(int *src, int *dst, int expected_val);
int frequeue(int *src, int *dst, int max_requeued_waiters, int expected_val);

struct list_head
{
    struct list_head *next;
    struct list_head *prev;
};

struct plist_node
{
    int prio;
    struct list_head prio_list;
    struct list_head node_list;
};

struct rt_mutex_waiter
{
    struct plist_node list_entry;
    struct plist_node pi_list_entry;
    struct task_struct *task;
    struct rt_mutex *lock;
};

struct thread_info
{
    struct task_struct *task;
    char padding[0x14];
    unsigned long addr_limit;
};

struct task_struct
{
    char pad1[0x238];
    struct task_struct *group_leader;
    char pad2[0xb0];
    struct cred *cred;
};

struct cred
{
    char pad[0x4];
    unsigned int ids[8];
};
