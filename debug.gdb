break futex_wait_requeue_pi
commands
set $waiter = &rt_waiter
cont
end

break ___sys_sendmsg
commands
p *$waiter
cont
end


break task_blocks_on_rt_mutex
