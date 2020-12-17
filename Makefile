all: privilege_escalation kernel_crash

privilege_escalation:
	gcc-5 -m32 -static privilege_escalation.c futex.c -o privilege_escalation.elf

kernel_crash:
	gcc-5 -m32 -static kernel_crash.c futex.c -o kernel_crash.elf

clean:
	rm -f *.elf
