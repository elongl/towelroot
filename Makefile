kernel_crash:
	gcc-5 -m32 -static kernel_crash.c futex.c -o kernel_crash.elf