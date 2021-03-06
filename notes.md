# Towelroot
> The futex_requeue function in kernel/futex.c in the Linux kernel through 3.14.5 does not ensure that calls have two different futex addresses, which allows local users to gain privileges via a crafted FUTEX_REQUEUE command that facilitates unsafe waiter modification.

---

## Kernel Crash
- [Parent] Locking the PI-aware futex. In userspace, atomically overwrite futex with the owner’s TID.
- [Child] Wait on a non-PI futex, and later requeue to a PI-aware futex.  
    - Create a futex queue entry and set its members to point to the stack variables.
    ```c
    struct futex_q q = futex_q_init;
    q.bitset = bitset;
    q.rt_waiter = &rt_waiter;
    q.requeue_pi_key = &key2;
    ```
    - Enqueue the futex queue entry (`futex_q`) onto the `futex_hash_bucket`.
    - Call `schedule()` and wait for the task to be flagged for rescheduling (sleep meanwhile).
- [Parent] Requeue the non-PI futex to the PI-aware futex.
    - Enqueue the `rt_mutex_waiter` to the `rt_mutex`.
    ```c
    plist_add(&waiter->list_entry, &lock->wait_list);
    ```
    - Requeue the futex queue entry to the destination futex bucket.
    ```c
    plist_del(&q->list, &hb1->chain);
	plist_add(&q->list, &hb2->chain);
    ```
- [Parent] Overwrite `pi_futex` with `0` in userspace (Manual Unlock).
```c
*pi_futex = 0;
```
- [Parent] Requeue the PI-aware futex to itself.
    - Take the lock on `pi_futex`.
    ```c
    if (unlikely(cmpxchg_futex_value_locked(&curval, uaddr, 0, newval)))
        return -EFAULT;

    ...

    /*
	 * Surprise - we got the lock. Just return to userspace:
	 */
	if (unlikely(!curval))
		return 1;
    ```
    - Wake up the child task.  
    `futex_wait_requeue_pi` exits without deleting `rt_waiter` from the `rt_mutex.wait_list`.  
    Kernel stack is overwritten and `rt_waiter` now points to garbage memory.  
    **Note**: The problem here is that the task should only wake up either because:
      - The lock was acquired, in which case `rt_waiter` is `NULL` and had already been removed from the `rt_mutex.wait_list`.
      - The lock was unlocked or the timeout was reached, which should issue the cleanup.

      Here, neither happens. Instead, the task wakes up and it wasn't removed from the waiters list on `rt_mutex`.
    ```c
    ret = futex_lock_pi_atomic(pifutex, hb2, key2, ps, top_waiter->task,
				   set_waiters); // Take lock in userspace.
	if (ret == 1)
		requeue_pi_wake_futex(top_waiter, key2, hb2); // <-- Wake child.
    ```
    (To Be Continued)


## Notes
- Add a `printk()` at each point within the flow.  
  Make sure the data changes accordingly in order to assess the control flow.
- Critical functions:
  - `futex_proxy_trylock_atomic`
  - `rt_mutex_start_proxy_lock`
  - `requeue_futex`


# Data Structures

```c
struct rt_mutex {
    raw_spinlock_t        wait_lock;
    struct plist_head    wait_list;
    struct task_struct    *owner;
};
```

`rt_mutex` is a mutex that implements priority inversion. `wait_lock` is a spinlock that serializes accesses to the mutex. `wait_list` is a linked list that contains `rt_waiter`s, one for each task that is currently blocked on the mutex. `owner` is the task that currently holds the mutex.

```c
struct rt_mutex_waiter {
    struct plist_node    list_entry;
    struct plist_node    pi_list_entry;
    struct task_struct    *task;
    struct rt_mutex        *lock;
};
```

An `rt_mutex_waiter` represents a thread that is currently blocked on an `rt_mutex`. These are the data structures that are inserted into the `wait_list` of the `rt_mutex`.

```c
union futex_key {
    struct {
        unsigned long pgoff;
        struct inode *inode;
        int offset;
    } shared;
    struct {
        unsigned long address;
        struct mm_struct *mm;
        int offset;
    } private;
    struct {
        unsigned long word;
        void *ptr;
        int offset;
    } both;
};
```

`futex_key` is a unique identifier for a futex, roughly equivalent to the physical address of the futex.
```c
struct futex_q {
    struct plist_node list;

    struct task_struct *task;
    spinlock_t *lock_ptr;
    union futex_key key;
    struct futex_pi_state *pi_state;
    struct rt_mutex_waiter *rt_waiter;
    union futex_key *requeue_pi_key;
    u32 bitset;
};

struct futex_hash_bucket {
    spinlock_t lock;
    struct plist_head chain;
};

static struct futex_hash_bucket futex_queues[1<<FUTEX_HASHBITS];
```


`futex_q` represents a thread that is waiting on a futex. They are held in a hash table (`futex_queues`) indexed by `futex_keys` that resolves collisions with separate chaining. `list` is the struct holding the links to the other elements in the same bucket. `task` is a pointer to the `task_struct` of the blocked thread. `rt_waiter` points to this thread's `rt_mutex_waiter`. This is only not `NULL` when the task is waiting to be requeued to a PI mutex through `futex_wait_requeue_pi`, or when it is waiting on a PI futex after being requeued (but not when it is waiting on a PI futex after `FUTEX_LOCK_PI` because in that case the `rt_mutex_waiter` is allocated on the stack of `rt_mutex_slowlock`). `requeue_pi_key` is the `futex_key` of the `uaddr2` that was specified in `futex_wait_requeue_pi`. This is to make sure that the thread gets requeued to that futex, and not to another.

```c
struct futex_pi_state {
    struct list_head list;

    struct rt_mutex pi_mutex;

    struct task_struct *owner;
    atomic_t refcount;

    union futex_key key;
};
```

`pi_state` contains the additional state that PI futexes need. The kernel allocates one for each locked PI futex that has at least 1 waiter. It is not allocated until at least 1 thread is blocked on this PI futex. It contains the `pi_mutex` that the kernel uses to implement priority inversion and that other threads waiting on this futex will queue on.
