$(shell mkdir -p bin)

all: privilege_escalation kernel_crash

privilege_escalation:
	gcc-5 -m32 -static -pthread privilege_escalation.c futex.c -o bin/privilege_escalation

kernel_crash:
	gcc-5 -m32 -static kernel_crash.c futex.c -o bin/kernel_crash

clean:
	rm -rf bin
