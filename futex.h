#include <stdint.h>

int futex(int *uaddr, int futex_op, int val, uint32_t val2, int *uaddr2, int val3);
void flock(int *uaddr);
void funlock(int *uaddr);
void fwait_requeue(int *src, int *dst, int expected_val);
void frequeue(int *src, int *dst, int max_requeued_waiters, int expected_val);
