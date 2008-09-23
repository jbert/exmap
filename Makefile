# Kernel modules
obj-m += exmap.o
obj-m += swapout.o
# These flags are for the kernel module(s) only
#EXTRA_CFLAGS=-g -O0

all: kernel_modules userspace

kernel_modules:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

userspace:
	make -f Makefile.user

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	make -f Makefile.user clean

test:
	make -f Makefile.user test
