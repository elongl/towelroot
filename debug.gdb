break futex_wait_requeue_pi
commands
set $waiter = &rt_waiter
cont
end

b ___sys_sendmsg
commands
p *$waiter
cont
end
